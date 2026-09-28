// IMP-004a：五库共享骨架（DD-001 §7；DR-004/008/010/011/012）
// 覆盖：导入（论文 never_delete、来源、版本）、版本不可变与当前版本切换、
//       停用不删除、有类型关系与自关系拒绝、证据链、方法步骤、贴士、方案。
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
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

class TstKnowledge : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_knowledge.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void importPaperWithSourceAndVersion()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        auto ftsIndex = Infrastructure::SqlKnowledgeFtsIndex(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);

        Application::KnowledgeUseCases::ImportInput input;
        input.libraryType = Domain::LibraryType::Paper;
        input.title = "Control theory: A useful conceptual framework";
        input.domainCode = "learning";
        input.summary = QStringLiteral("负反馈回路与层级控制").toStdString();
        input.contentHash = "hash-carver-1982";
        input.createdBy = "developer";

        Domain::SourceRecord source;
        source.sourceType = "doi";
        source.canonicalUri = "10.1037/0033-2909.92.1.111";
        source.title = input.title;
        source.contentHash = input.contentHash;
        source.trustTier = "vetted";
        input.source = source;

        Domain::PaperDetail paper;
        paper.studyType = "review";
        input.paper = paper;

        const auto imported = useCases.importKnowledge(input);
        if (!imported)
            QFAIL(qPrintable(QString::fromStdString(imported.error().message + ": "
                                                    + imported.error().detail)));
        m_paperUid = QString::fromStdString(imported.value().item.uid.value());
        // 论文强制 never_delete
        QVERIFY(imported.value().item.neverDelete);
        QVERIFY(imported.value().item.currentVersionUid.has_value());
        QCOMPARE(repo.versionsOf(imported.value().item.uid).size(), 1);
    }

    void versionImmutabilityAndPointerSwitch()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        auto ftsIndex = Infrastructure::SqlKnowledgeFtsIndex(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);
        const auto itemUid = *Domain::Uid::parse(m_paperUid.toStdString());

        // 追加新版本并切换当前版本
        Application::KnowledgeUseCases::VersionInput v2;
        v2.summary = QStringLiteral("修订版：补充层级控制细节").toStdString();
        v2.contentHash = "hash-carver-1982-v2";
        v2.isActive = true;
        const auto added = useCases.addVersion(itemUid, 2, v2);
        if (!added)
            QFAIL(qPrintable(QString::fromStdString(added.error().message + ": "
                                                    + added.error().detail)));
        QCOMPARE(added.value().versionNo, 2);

        // 版本不可变：旧版本内容未被覆盖，共两个版本
        const auto versions = repo.versionsOf(itemUid);
        QCOMPARE(versions.size(), 2);
        QVERIFY(versions[0].summary == inputSummaryV1());
        QVERIFY(versions[1].summary == v2.summary);
        // 当前版本指针指向 v2
        const auto item = repo.findItem(itemUid);
        QVERIFY(item && item->currentVersionUid
                && *item->currentVersionUid == versions[1].uid.value());

        // 版本插入后若当前指针的 revision 守卫失败，新版本必须整体回滚。
        Application::KnowledgeUseCases::VersionInput rejected;
        rejected.summary = "must roll back";
        rejected.contentHash = "hash-carver-rejected";
        rejected.isActive = true;
        const auto failed = useCases.addVersion(itemUid, 999, rejected);
        QVERIFY(!failed.hasValue());
        QCOMPARE(repo.versionsOf(itemUid).size(), size_t(2));
        const auto unchanged = repo.findItem(itemUid);
        QVERIFY(unchanged && unchanged->currentVersionUid
                && *unchanged->currentVersionUid == versions[1].uid.value());
    }

    void deprecateIsNotDelete()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        auto ftsIndex = Infrastructure::SqlKnowledgeFtsIndex(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);
        const auto itemUid = *Domain::Uid::parse(m_paperUid.toStdString());

        const auto item = repo.findItem(itemUid);
        const auto archived =
            useCases.deprecate(itemUid, item->revision, Domain::KnowledgeStatus::Archived);
        if (!archived)
            QFAIL(qPrintable(QString::fromStdString(archived.error().message + ": "
                                                    + archived.error().detail)));
        // 数据仍在（不物理删除），仅状态变化
        const auto after = repo.findItem(itemUid);
        QVERIFY(after.has_value());
        QVERIFY(after->status == Domain::KnowledgeStatus::Archived);
        QVERIFY(!repo.versionsOf(itemUid).empty());
    }

    void relationsAndEvidenceChain()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        auto ftsIndex = Infrastructure::SqlKnowledgeFtsIndex(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);

        // 第二个条目（方法）
        Application::KnowledgeUseCases::ImportInput input;
        input.libraryType = Domain::LibraryType::Method;
        input.title = QStringLiteral("检索练习法").toStdString();
        input.domainCode = "learning";
        input.summary = QStringLiteral("低风险练习方法").toStdString();
        input.contentHash = "hash-retrieval-practice";
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
        step.instruction = QStringLiteral("合上书回忆要点").toStdString();
        step.sequenceNo = 0;
        input.methodSteps = {step};
        const auto imported = useCases.importKnowledge(input);
        if (!imported)
            QFAIL(qPrintable(QString::fromStdString(imported.error().message + ": "
                                                    + imported.error().detail)));
        const auto methodUid = imported.value().item.uid;
        QVERIFY(imported.value().item.neverDelete);

        // 有类型关系：方法 derived_from 论文
        Domain::KnowledgeRelation relation;
        relation.fromItemUid = methodUid.value();
        relation.toItemUid = m_paperUid.toStdString();
        relation.relation = "derived_from";
        relation.confidence = 0.9;
        const auto related = useCases.relate(relation);
        if (!related)
            QFAIL(qPrintable(QString::fromStdString(related.error().message + ": "
                                                    + related.error().detail)));
        const auto relations = repo.relationsOf(methodUid);
        QCOMPARE(relations.size(), 1);
        QVERIFY(relations[0].relation == "derived_from");

        // 自关系拒绝
        Domain::KnowledgeRelation self;
        self.fromItemUid = methodUid.value();
        self.toItemUid = methodUid.value();
        self.relation = "supports";
        QVERIFY(!useCases.relate(self));

        // 证据链：来源 → 片段 → 版本
        Domain::EvidenceFragment fragment;
        fragment.uid = m_uids.next();
        fragment.sourceUid = *Domain::Uid::parse(
            [&] {
                QSqlQuery q(DatabaseManager::instance().database());
                q.exec(QStringLiteral("SELECT uid FROM source_records_v5 WHERE canonical_uri="
                                      "'10.1177/1529100612453266'"));
                q.next();
                return q.value(0).toString();
            }()
                .toStdString());
        fragment.locatorJson = std::string("{\"page\":3}");
        fragment.text = QStringLiteral("Testing effect: retrieval practice improves retention.")
                            .toStdString();
        fragment.fragmentHash = "frag-1";
        if (!repo.insertFragment(fragment).ok)
            QFAIL("fragment insert failed");

        Domain::EvidenceLink link;
        link.knowledgeVersionUid = imported.value().versionUid;
        link.fragmentUid = fragment.uid.value();
        link.relation = "supports";
        link.strength = 0.95;
        const auto linked = useCases.linkEvidence(link);
        if (!linked)
            QFAIL(qPrintable(QString::fromStdString(linked.error().message + ": "
                                                    + linked.error().detail)));
    }

    void failedCompositeImportRollsBackAllRows()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex ftsIndex(db, m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);
        QSqlQuery count(db);
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM knowledge_items_v5")));
        QVERIFY(count.next());
        const int before = count.value(0).toInt();

        Application::KnowledgeUseCases::ImportInput input;
        input.libraryType = Domain::LibraryType::Method;
        input.title = "Transaction rollback method";
        input.domainCode = "research.test";
        input.summary = "must not remain after a late step failure";
        input.contentHash = "transaction-rollback-method";
        input.createdBy = "generated";
        Domain::MethodDetail detail;
        detail.methodType = "test";
        detail.riskLevel = "low";
        detail.evidenceGrade = "test";
        input.method = detail;
        Domain::MethodStep valid;
        valid.sequenceNo = 0;
        valid.instruction = "valid first step";
        Domain::MethodStep invalid;
        invalid.sequenceNo = 0; // 与首步冲突，在最后阶段触发数据库唯一约束。
        invalid.instruction = "duplicate sequence";
        input.methodSteps = {valid, invalid};
        const auto imported = useCases.importKnowledge(input);
        QVERIFY(!imported.hasValue());

        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM knowledge_items_v5")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), before);
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_fts_v6 WHERE title='Transaction rollback method'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 0);
    }

    void tipAndPlanDetails()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        auto ftsIndex = Infrastructure::SqlKnowledgeFtsIndex(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, ftsIndex, m_uids, m_clock);

        Application::KnowledgeUseCases::ImportInput tipInput;
        tipInput.libraryType = Domain::LibraryType::Tip;
        tipInput.title = QStringLiteral("番茄钟试用心得").toStdString();
        tipInput.domainCode = "learning";
        tipInput.summary = QStringLiteral("个人经验：25 分钟分段有帮助").toStdString();
        tipInput.contentHash = "hash-tip-pomodoro";
        tipInput.createdBy = "user";
        Domain::SourceRecord tipSource;
        tipSource.sourceType = "manual";
        tipSource.title = tipInput.title;
        tipSource.contentHash = tipInput.contentHash;
        tipSource.trustTier = "unverified";
        tipInput.source = tipSource;
        Domain::TipDetail tip;
        tip.captureType = "personal_experience";
        tip.verificationStatus = "personal_experience";
        tip.riskLevel = "low";
        tipInput.tip = tip;
        const auto tipImported = useCases.importKnowledge(tipInput);
        if (!tipImported)
            QFAIL(qPrintable(QString::fromStdString(tipImported.error().message + ": "
                                                    + tipImported.error().detail)));
        QVERIFY(!tipImported.value().item.neverDelete);   // 贴士可删除（状态替代）

        Application::KnowledgeUseCases::ImportInput planInput;
        planInput.libraryType = Domain::LibraryType::Plan;
        planInput.title = QStringLiteral("计算机考研基础路线").toStdString();
        planInput.domainCode = "learning";
        planInput.summary = QStringLiteral("阶段化路线模板").toStdString();
        planInput.contentHash = "hash-plan-cs";
        planInput.createdBy = "user";
        Domain::SourceRecord planSource;
        planSource.sourceType = "manual";
        planSource.title = planInput.title;
        planSource.contentHash = planInput.contentHash;
        planSource.trustTier = "unverified";
        planInput.source = planSource;
        Domain::PlanDetail plan;
        plan.targetProfileJson = std::string("{\"stage\":\"undergraduate\"}");
        planInput.plan = plan;
        const auto planImported = useCases.importKnowledge(planInput);
        if (!planImported)
            QFAIL(qPrintable(QString::fromStdString(planImported.error().message + ": "
                                                    + planImported.error().detail)));
    }

private:
    static std::string inputSummaryV1()
    {
        return QStringLiteral("负反馈回路与层级控制").toStdString();
    }

    QString m_path;
    QString m_paperUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstKnowledge)
#include "tst_knowledge.moc"
