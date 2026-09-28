// IMP-004b：FTS5 索引服务与受管文件资产（数据库设计 §6；DD-001 §8.1）
// 覆盖：导入即索引、全文检索（中英文）、覆盖式更新、全量重建、一致性检查、
//       文件隔离区登记→正式化、SHA-256 查重。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlFileAssetRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

class TstKnowledgeFts : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_knowledge_fts.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void importIndexesAndSearchFindsHits()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::KnowledgeUseCases useCases(repo, fts, m_uids, m_clock);

        Application::KnowledgeUseCases::ImportInput input;
        input.libraryType = Domain::LibraryType::Method;
        input.title = QStringLiteral("间隔复习").toStdString();
        input.domainCode = "learning";
        input.summary = QStringLiteral("分散练习优于集中练习，回忆测试提升保持率").toStdString();
        input.claimsJson =
            QStringLiteral("[\"Spaced practice improves long-term retention\"]").toStdString();
        input.contentHash = "hash-spaced";
        input.createdBy = "developer";
        Domain::SourceRecord source;
        source.sourceType = "doi";
        source.canonicalUri = "10.1177/1529100612453266";
        source.title = input.title;
        source.contentHash = input.contentHash;
        source.trustTier = "vetted";
        input.source = source;
        Domain::MethodDetail method;
        method.methodType = "learning_technique";
        method.riskLevel = "low";
        method.evidenceGrade = "A";
        input.method = method;
        Domain::MethodStep step;
        step.instruction = QStringLiteral("按遗忘曲线安排复习").toStdString();
        input.methodSteps = {step};
        const auto imported = useCases.importKnowledge(input);
        if (!imported)
            QFAIL(qPrintable(QString::fromStdString(imported.error().message + ": "
                                                    + imported.error().detail)));
        m_methodUid = QString::fromStdString(imported.value().item.uid.value());

        // 中文检索命中（unicode61 下 CJK 连续串为单 token；完整词或前缀查询）
        const auto hits = fts.search(QStringLiteral("间隔复习").toStdString(), 10);
        bool found = false;
        for (const auto &hit : hits)
            if (hit.ownerUid == imported.value().item.uid.value())
                found = true;
        QVERIFY(found);

        // 英文检索命中（claims 字段内的英文）
        const auto hits2 = fts.search(QStringLiteral("spaced").toStdString(), 10);
        bool found2 = false;
        for (const auto &hit : hits2)
            if (hit.ownerUid == imported.value().item.uid.value())
                found2 = true;
        QVERIFY(found2);

        // 一致性：索引条目数 == 正式条目数
        QVERIFY(fts.isConsistent());
    }

    void rebuildRestoresIndex()
    {
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(),
                                                 m_clock);
        // 破坏索引后全量重建
        QSqlQuery wipe(DatabaseManager::instance().database());
        QVERIFY(wipe.exec(QStringLiteral("DELETE FROM knowledge_fts_v6")));
        QVERIFY(!fts.isConsistent());

        const auto rebuilt = fts.rebuildAll();
        QVERIFY2(rebuilt.ok,
                 qPrintable(QString::fromStdString(rebuilt.error.message + ": "
                                                   + rebuilt.error.detail)));
        QVERIFY(fts.isConsistent());
        // CJK 二元索引：按二元组召回（"复习"为索引中的 bigram）
        const auto hits = fts.search(QStringLiteral("复习").toStdString(), 10);
        bool found = false;
        for (const auto &hit : hits)
            if (hit.ownerUid == m_methodUid.toStdString())
                found = true;
        QVERIFY(found);
    }

    void fileAssetQuarantinePromoteAndDedup()
    {
        Infrastructure::SqlFileAssetRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);

        Domain::FileAsset asset;
        asset.uid = m_uids.next();
        asset.relativePath = QStringLiteral("papers/a8f3-间隔复习.pdf").toStdString();
        asset.mimeType = "application/pdf";
        asset.byteSize = 1024;
        asset.sha256 = "sha256-file-0001";
        asset.originalName = QStringLiteral("间隔复习论文.pdf").toStdString();

        const auto registered = repo.registerAsset(asset);
        if (!registered)
            QFAIL(qPrintable(QString::fromStdString(registered.error().message + ": "
                                                    + registered.error().detail)));
        QVERIFY(registered.value().storageState == Domain::FileStorageState::Quarantine);

        // SHA-256 查重：同指纹重复注册返回同一资产
        Domain::FileAsset duplicate = asset;
        duplicate.uid = m_uids.next();
        const auto dedup = repo.registerAsset(duplicate);
        QVERIFY(dedup);
        QVERIFY(dedup.value().uid == registered.value().uid);

        // 隔离区 → 正式化（revision 守卫）
        const auto promoted = repo.promote(registered.value().uid, 1);
        if (!promoted)
            QFAIL(qPrintable(QString::fromStdString(promoted.error().message + ": "
                                                    + promoted.error().detail)));
        QVERIFY(promoted.value().storageState == Domain::FileStorageState::Managed);
        // 已正式化资产不可重复 promote
        const auto again = repo.promote(registered.value().uid, 2);
        QVERIFY(!again);
        QVERIFY(again.error().code == Application::ErrorCode::Conflict);
    }

private:
    QString m_path;
    QString m_methodUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstKnowledgeFts)
#include "tst_knowledge_fts.moc"
