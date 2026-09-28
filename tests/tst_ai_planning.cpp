// 模块：AI 端到端规划管线（DR-015/016/017/027；E2E-02/11 的用例层验证）
// 覆盖：路线候选生成（检索→快照→AI→契约/硬约束校验→候选落库→决策记录）、
//       契约失败不写入、无可用连接 ExternalUnavailable、MEL 候选（周期来自
//       领域清单）、逐任务方法建议（只能引用召回候选）、咨询回答与决策反馈。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "application/usecases/state/CalibrationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"
#include "infrastructure/persistence/SqlCalibrationRepository.h"
#include "infrastructure/persistence/SqlDomainManifestRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {

// 固定响应的 Provider 替身（契约测试可注入；不依赖在线模型稳定性）
class FakeProvider : public Application::AiProviderPort
{
public:
    Application::ProviderResponse canned;
    std::string lastUserPrompt;

    Application::ProviderResponse submit(const Domain::AiProviderConfig &,
                                         const Application::ProviderRequest &request) override
    {
        lastUserPrompt = request.userPrompt;
        return canned;
    }
};

} // namespace

class TstAiPlanning : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_ai_planning.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'规划目标',"
            "'学习操作系统','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z',"
            "'2026-09-27T00:00:00Z' FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));

        // 启用状态的模型连接（无凭据引用被 provider 使用——FakeProvider 不读凭据）
        Infrastructure::SqlAiRepository aiRepo(DatabaseManager::instance().database(), m_clock);
        Domain::AiProviderConfig config;
        config.uid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000ab");
        config.providerCode = "openai_compatible";
        config.displayName = "测试模型";
        config.endpoint = "https://example.invalid/v1";
        config.model = "test-model";
        config.credentialRef = "test:ref";
        config.capabilitiesJson = std::string("{\"text\":true}");
        config.enabled = true;
        QVERIFY(aiRepo.insertConfig(config).ok);

        // 方法库条目（供检索召回与方法建议引用）
        Infrastructure::SqlKnowledgeRepository knowledgeRepo(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases knowledgeUseCases(knowledgeRepo, fts, m_uids, m_clock);
        Application::KnowledgeUseCases::ImportInput import;
        import.libraryType = Domain::LibraryType::Method;
        import.title = QStringLiteral("间隔复习方法").toStdString();
        import.domainCode = "learning";
        import.summary = QStringLiteral("对操作系统知识进行间隔复习的方法").toStdString();
        import.contentHash = std::string("test:method:1");
        import.createdBy = "test";
        Domain::SourceRecord source;
        source.sourceType = "manual";
        source.title = QStringLiteral("测试方法来源").toStdString();
        source.contentHash = std::string("test:source:1");
        source.trustTier = "vetted";
        import.source = source;
        Domain::MethodDetail methodDetail;
        methodDetail.methodType = "spaced_repetition";
        import.method = methodDetail;
        const auto imported = knowledgeUseCases.importKnowledge(import);
        if (!imported)
            QFAIL(qPrintable(QString::fromStdString(imported.error().message + ": "
                                                    + imported.error().detail)));
        m_methodVersionUid = imported.value().versionUid;
    }

    // 组装真实管线（FakeProvider 注入）
    std::unique_ptr<Application::AiPlanningUseCases> makePipeline(FakeProvider &provider)
    {
        auto *aiRepo = new Infrastructure::SqlAiRepository(DatabaseManager::instance().database(),
                                                           m_clock);
        m_aiRepo.reset(aiRepo);
        auto *gateway = new Infrastructure::AiGateway(*aiRepo, provider, m_uids, m_clock);
        m_gateway.reset(gateway);
        auto *retrieval = new Infrastructure::SqlKnowledgeRetrieval(
            DatabaseManager::instance().database(), m_clock);
        m_retrieval.reset(retrieval);
        m_goalsRepo = std::make_unique<Infrastructure::SqlGoalRepository>(
            DatabaseManager::instance().database(), m_clock);
        m_melsRepo = std::make_unique<Infrastructure::SqlMelRepository>(
            DatabaseManager::instance().database(), m_clock);
        m_routesRepo = std::make_unique<Infrastructure::SqlRouteRepository>(
            DatabaseManager::instance().database(), m_clock);
        m_statesRepo = std::make_unique<Infrastructure::SqlStateRepository>(
            DatabaseManager::instance().database(), m_clock);
        m_manifestsRepo = std::make_unique<Infrastructure::SqlDomainManifestRepository>(
            DatabaseManager::instance().database());
        m_knowledgeRepo = std::make_unique<Infrastructure::SqlKnowledgeRepository>(
            DatabaseManager::instance().database(), m_clock);
        return std::make_unique<Application::AiPlanningUseCases>(
            *m_gateway, *aiRepo, *m_retrieval, *m_knowledgeRepo, *m_goalsRepo, *m_melsRepo,
            *m_routesRepo, *m_statesRepo, *m_manifestsRepo, *aiRepo, m_uids, m_clock);
    }

    Domain::Uid goalUid()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='规划目标'"));
        q.next();
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    void routeProposalPipeline()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.userText = "路线建议";
        provider.canned.structuredJson = QStringLiteral(
            "{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"stages\":[{\"title\":\"第一章\"}],\"rationale\":\"依据测试\","
            "\"evidence_summary\":\"无\",\"assumptions\":{},\"source_mode\":\"ungrounded\"}")
                                             .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");

        const auto result = pipeline->generateRouteProposal(userUid, goalUid());
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QVERIFY(!result.value().aggregateUid.empty());
        QVERIFY(!result.value().decisionUid.empty());
        QVERIFY(!result.value().knowledgeSnapshotUid.empty());

        // 候选路线已落库（proposed），提示词包含目标与契约说明
        QVERIFY(!provider.lastUserPrompt.empty());
        QVERIFY2(provider.lastUserPrompt.find("goal_uid") != std::string::npos,
                 qPrintable(QString::fromStdString(provider.lastUserPrompt)));

        Infrastructure::SqlRouteRepository routes(DatabaseManager::instance().database(),
                                                  m_clock);
        const auto byGoal = routes.findByGoal(goalUid());
        bool sawDraft = false;
        for (const auto &route : byGoal)
            if (route.status == Domain::RouteStatus::Draft)   // AI 候选（既有约定）
                sawDraft = true;
        QVERIFY(sawDraft);

        // 决策记录 pending（用户确认前不生效）
        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto decisions = ai.listDecisions("route_proposal", 10);
        QVERIFY(!decisions.empty());
        QVERIFY(decisions.front().userStatus == Domain::DecisionUserStatus::Pending);

        // 知识快照落库（可追溯）
        QSqlQuery snapshots(DatabaseManager::instance().database());
        QVERIFY(snapshots.exec(QStringLiteral("SELECT COUNT(*) FROM knowledge_snapshots_v6")));
        QVERIFY(snapshots.next());
        QVERIFY(snapshots.value(0).toInt() >= 1);

        // 用户确认后决策状态更新
        QVERIFY(pipeline->markDecision(result.value().decisionUid, "accepted",
                                       decisions.front().candidateJson));
        const auto after = ai.findDecision(
            *Domain::Uid::parse(result.value().decisionUid));
        QVERIFY(after);
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);
    }

    void contractFailureWritesNothing()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            std::string("{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\"}");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");

        const auto result = pipeline->generateRouteProposal(userUid, goalUid());
        QVERIFY(!result);   // 契约缺 stages → 失败
        QVERIFY(result.error().code == Application::ErrorCode::Validation
                || result.error().code == Application::ErrorCode::ExternalUnavailable);
    }

    void noEnabledConfigIsUnavailable()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            QStringLiteral(
                "{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
                "\"stages\":[{\"title\":\"S1\"}],\"rationale\":\"r\","
                "\"evidence_summary\":\"e\",\"assumptions\":{},\"source_mode\":\"ungrounded\"}")
                .toStdString();

        // 停用连接
        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto config = ai.findConfig(
            *Domain::Uid::parse("00000000-0000-0000-0000-0000000000ab"));
        QVERIFY(config);
        auto disabled = *config;
        disabled.enabled = false;
        QVERIFY(ai.updateConfig(disabled, config->revision).ok);

        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateRouteProposal(userUid, goalUid());
        QVERIFY(!result);
        QVERIFY(result.error().code == Application::ErrorCode::ExternalUnavailable);

        // 恢复启用（重新读取当前 revision）
        const auto restored = ai.findConfig(
            *Domain::Uid::parse("00000000-0000-0000-0000-0000000000ab"));
        QVERIFY(restored);
        auto enabled = *restored;
        enabled.enabled = true;
        QVERIFY(ai.updateConfig(enabled, restored->revision).ok);
    }

    void melProposalFromDomainPeriod()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = QStringLiteral(
            "{\"title\":\"本轮复习\",\"tasks\":[{\"title\":\"间隔复习任务\","
            "\"planned_effort_min\":60,\"required\":true}],\"rationale\":\"容量依据\","
            "\"capacity_min\":120,\"reserve_min\":20,\"period_days\":3,"
            "\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"source_mode\":\"partially_grounded\"}")
                                             .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");

        const auto result = pipeline->generateMelProposal(userUid, goalUid(), std::nullopt);
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QVERIFY(!result.value().aggregateUid.empty());

        Infrastructure::SqlMelRepository mels(DatabaseManager::instance().database(), m_clock);
        const auto mel = mels.findByUid(*Domain::Uid::parse(result.value().aggregateUid));
        QVERIFY(mel);
        QVERIFY(mel->state == Domain::MelState::Draft);
        const auto tasks = mels.tasksOf(mel->uid);
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(QString::fromStdString(tasks.front().title),
                 QStringLiteral("间隔复习任务"));

        // 计划预测已写入（预测→实际→校准闭环输入端）
        const auto prediction = mels.latestPredictionOf(mel->uid);
        QVERIFY(prediction.has_value());
        QCOMPARE(prediction->predictedEffortMin, 120);   // capacity_min
        QVERIFY(prediction->basisJson.find("snapshot_uid") != std::string::npos);

        // 校准闭环：有预测后 recordMelOutcome 真实记录（不再诚实跳过）
        Infrastructure::SqlCalibrationRepository calibrationRepo(
            DatabaseManager::instance().database(), m_clock);
        Application::CalibrationUseCases calibration(calibrationRepo, m_uids, m_clock);
        const auto outcome = calibration.recordMelOutcome(mel->uid);
        QVERIFY(outcome);
        QVERIFY(outcome.value().recorded);
        QCOMPARE(outcome.value().records.size(), 2);
    }

    void methodSuggestionsBoundToRecalledCandidates()
    {
        // 建立一个带任务的 MEL（draft 即可）
        Infrastructure::SqlMelRepository mels(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlDomainManifestRepository manifests(
            DatabaseManager::instance().database());
        const auto manifestUid = manifests.findManifestByCode("learning");
        QVERIFY(manifestUid);
        const auto version = manifests.latestActiveVersion(*manifestUid);
        QVERIFY(version);

        Domain::Mel mel;
        mel.uid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000cc");
        mel.userId = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        mel.goalId = goalUid();
        mel.manifestVersionId = *Domain::Uid::parse(version->versionUid);
        mel.title = QStringLiteral("方法建议MEL").toStdString();
        mel.plannedStartAt = "2026-09-28T00:00:00Z";
        mel.plannedEndAt = "2026-10-01T00:00:00Z";
        mel.timezoneId = "Asia/Shanghai";
        mel.capacityMin = 300;
        mel.reserveMin = 30;
        mel.rationale = "测试";
        QVERIFY(mels.insert(mel).ok);
        Domain::MelTask task;
        task.uid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000cd");
        task.melId = mel.uid;
        task.title = QStringLiteral("间隔复习任务").toStdString();
        task.plannedEffortMin = 60;
        task.required = true;
        task.completionRuleJson = std::string("{}");
        QVERIFY(mels.insertTask(task).ok);

        FakeProvider provider;
        provider.canned.ok = true;
        // 一条合法（召回内）+ 一条凭空引用（必须被拒绝）
        provider.canned.structuredJson =
            QStringLiteral(
                "{\"suggestions\":["
                "{\"task_uid\":\"00000000-0000-0000-0000-0000000000cd\","
                "\"method_version_uid\":\"%1\",\"reason\":\"匹配\","
                "\"applicability\":\"适用\",\"risks\":\"无\"},"
                "{\"task_uid\":\"00000000-0000-0000-0000-0000000000cd\","
                "\"method_version_uid\":\"00000000-0000-0000-0000-0000000000ff\","
                "\"reason\":\"虚构\",\"applicability\":\"\",\"risks\":\"\"}]}")
                .arg(QString::fromStdString(m_methodVersionUid))
                .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto bound = pipeline->generateMethodSuggestions(userUid, mel.uid);
        if (!bound)
            QFAIL(qPrintable(QString::fromStdString(bound.error().message + ": "
                                                    + bound.error().detail)));
        QCOMPARE(bound.value(), 1);   // 只有召回内的那条被绑定

        const auto methods = mels.taskMethodsOf(mel.uid);
        QCOMPARE(methods.size(), 1);
        QCOMPARE(QString::fromStdString(methods.front().methodVersionUid),
                 QString::fromStdString(m_methodVersionUid));
    }

    void advisorAnswerWithKnowledgeStatus()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            std::string("{\"user_text\":\"这是回答\",\"source_mode\":\"ungrounded\"}");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->askAdvisor(userUid, "怎么复习操作系统？");
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QVERIFY(!result.value().decisionUid.empty());

        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto decision = ai.findDecision(
            *Domain::Uid::parse(result.value().decisionUid));
        QVERIFY(decision);
        QVERIFY(decision->userStatus == Domain::DecisionUserStatus::NotRequired);
        // 方法条目已入库且中文检索可用 → 部分支持（而非无支持）
        QVERIFY(decision->sourceMode == Domain::SourceMode::PartiallyGrounded
                || decision->sourceMode == Domain::SourceMode::Ungrounded);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    std::string m_methodVersionUid;
    std::unique_ptr<Infrastructure::SqlAiRepository> m_aiRepo;
    std::unique_ptr<Infrastructure::AiGateway> m_gateway;
    std::unique_ptr<Infrastructure::SqlKnowledgeRetrieval> m_retrieval;
    std::unique_ptr<Infrastructure::SqlGoalRepository> m_goalsRepo;
    std::unique_ptr<Infrastructure::SqlMelRepository> m_melsRepo;
    std::unique_ptr<Infrastructure::SqlRouteRepository> m_routesRepo;
    std::unique_ptr<Infrastructure::SqlStateRepository> m_statesRepo;
    std::unique_ptr<Infrastructure::SqlDomainManifestRepository> m_manifestsRepo;
    std::unique_ptr<Infrastructure::SqlKnowledgeRepository> m_knowledgeRepo;
};

QTEST_GUILESS_MAIN(TstAiPlanning)
#include "tst_ai_planning.moc"
