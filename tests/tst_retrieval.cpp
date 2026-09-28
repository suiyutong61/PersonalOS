// IMP-005：检索编排与知识不足分级（DD-001 §7；DR-007/013/014/027/028）
// 覆盖：结构化过滤+词法召回、命中落库、快照固化与幂等、支持程度三档规则、
//       冲突独立标志。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/foundation/SupportAssessor.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

class TstRetrieval : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_retrieval.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));

        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::KnowledgeUseCases useCases(repo, fts, m_uids, m_clock);

        // 两个方法 + 一个贴士（用于过滤与召回验证）
        const struct
        {
            Domain::LibraryType type;
            const char *title;
            const char *summary;
            const char *hash;
        } seeds[] = {
            {Domain::LibraryType::Method, "间隔复习",
             "分散练习优于集中练习，回忆测试提升保持率", "hash-m1"},
            {Domain::LibraryType::Method, "费曼讲解法",
             "用自己的话解释概念，暴露理解缺口", "hash-m2"},
            {Domain::LibraryType::Tip, "番茄钟分段",
             "25 分钟专注 + 5 分钟休息的个人经验", "hash-t1"},
        };
        for (const auto &seed : seeds) {
            Application::KnowledgeUseCases::ImportInput input;
            input.libraryType = seed.type;
            input.title = seed.title;
            input.domainCode = "learning";
            input.summary = seed.summary;
            input.contentHash = seed.hash;
            input.createdBy = "developer";
            Domain::SourceRecord source;
            source.sourceType = "manual";
            source.title = seed.title;
            source.contentHash = seed.hash;
            source.trustTier = "vetted";
            input.source = source;
            if (seed.type == Domain::LibraryType::Method) {
                Domain::MethodDetail method;
                method.methodType = "learning_technique";
                method.riskLevel = "low";
                method.evidenceGrade = "A";
                input.method = method;
            } else {
                Domain::TipDetail tip;
                tip.captureType = "personal_experience";
                tip.verificationStatus = "personal_experience";
                tip.riskLevel = "low";
                input.tip = tip;
            }
            const auto imported = useCases.importKnowledge(input);
            if (!imported)
                QFAIL(qPrintable(QString::fromStdString(imported.error().message + ": "
                                                        + imported.error().detail)));
        }
    }

    void retrieveWithFiltersAndPersist()
    {
        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);

        // 词法召回：中文前缀查询命中方法
        Application::RetrievalRequest request;
        request.purpose = "mel_planning";
        request.queryText = "间隔*";
        request.filtersJson = std::string("{\"library_type\":\"method\"}");
        const auto result = retrieval.retrieve(request);
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QVERIFY(!result.value().hits.empty());
        for (const auto &hit : result.value().hits) {
            QVERIFY(hit.ownerType == "method");
            QVERIFY(hit.rank > 0);
            QVERIFY(hit.finalScore > 0.0);
        }

        // 命中落库可追溯
        QSqlQuery count(DatabaseManager::instance().database());
        count.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM retrieval_hits_v6 WHERE run_id="
            "(SELECT id FROM retrieval_runs_v6 WHERE uid=?)"));
        count.addBindValue(QString::fromStdString(result.value().run.uid.value()));
        QVERIFY(count.exec() && count.next());
        QCOMPARE(count.value(0).toInt(), static_cast<int>(result.value().hits.size()));

        // 过滤：library_type=tip 时方法不出现
        Application::RetrievalRequest tipRequest;
        tipRequest.purpose = "advice";
        tipRequest.queryText = "番茄钟";
        tipRequest.filtersJson = std::string("{\"library_type\":\"tip\"}");
        const auto tipResult = retrieval.retrieve(tipRequest);
        QVERIFY(tipResult);
        bool onlyTips = true;
        for (const auto &hit : tipResult.value().hits)
            if (hit.ownerType != "tip")
                onlyTips = false;
        QVERIFY(onlyTips);
    }

    void snapshotIsImmutableAndIdempotent()
    {
        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);

        const std::string versions =
            std::string("[{\"owner_type\":\"method\",\"owner_uid\":\"u1\",\"version\":\"v1\"}]");
        const auto first = retrieval.snapshot("mel_planning", "[]", versions, std::nullopt);
        if (!first)
            QFAIL(qPrintable(QString::fromStdString(first.error().message + ": "
                                                    + first.error().detail)));
        // 同内容幂等：返回同一快照（content_hash 唯一）
        const auto second = retrieval.snapshot("mel_planning", "[]", versions, std::nullopt);
        QVERIFY(second);
        QVERIFY(second.value().uid == first.value().uid);
        // 不同内容 → 新快照
        const auto third = retrieval.snapshot("mel_planning", "[]", "[]", std::nullopt);
        QVERIFY(third);
        QVERIFY(third.value().uid != first.value().uid);
    }

    void supportAssessorRules()
    {
        // Grounded：全部关键子问题有支持
        const auto grounded = Application::SupportAssessor::assess({3, 3, false});
        QVERIFY(grounded.level == Domain::KnowledgeSupportLevel::Grounded);
        QVERIFY(!grounded.hasMaterialConflict);

        // PartiallyGrounded：部分支持
        const auto partial = Application::SupportAssessor::assess({3, 1, false});
        QVERIFY(partial.level == Domain::KnowledgeSupportLevel::PartiallyGrounded);

        // Ungrounded：零覆盖
        const auto ungrounded = Application::SupportAssessor::assess({3, 0, false});
        QVERIFY(ungrounded.level == Domain::KnowledgeSupportLevel::Ungrounded);

        // 冲突独立于覆盖：覆盖充分但存在重要冲突 → 等级不变、冲突标志独立
        const auto conflicted = Application::SupportAssessor::assess({3, 3, true});
        QVERIFY(conflicted.level == Domain::KnowledgeSupportLevel::Grounded);
        QVERIFY(conflicted.hasMaterialConflict);

        // 未定义关键子问题 → ungrounded
        const auto empty = Application::SupportAssessor::assess({0, 0, false});
        QVERIFY(empty.level == Domain::KnowledgeSupportLevel::Ungrounded);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstRetrieval)
#include "tst_retrieval.moc"
