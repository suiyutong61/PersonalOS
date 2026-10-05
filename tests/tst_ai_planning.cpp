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
#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "application/usecases/route/RouteStageDetailUseCases.h"
#include "application/usecases/route/RouteUseCases.h"
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
#include "infrastructure/persistence/SqlUnitOfWork.h"

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

// 阶段详情契约响应（数量全部在门禁范围内；资料子集由调用方给出）
std::string stageDetailJson(const std::string &stageUid, const std::string &materialsJson)
{
    return std::string("{\"stage_uid\":\"") + stageUid
           + "\",\"outcomes\":[{\"description\":\"能独立完成服务器基本操作\"}],"
           + "\"tasks\":[{\"sequence_no\":1,\"title\":\"命令行与文件系统\",\"description\":\"常用命令\",\"estimated_effort_min\":120},"
           + "{\"sequence_no\":2,\"title\":\"网络配置\",\"description\":\"ip/路由/DNS\",\"estimated_effort_min\":120}],"
           + "\"projects\":[{\"title\":\"搭建测试环境\",\"description\":\"安装并配置\",\"verifiable_result\":\"环境可访问\"}],"
           + "\"criteria\":[{\"description\":\"不看资料完成 10 个常用操作\"}],"
           + "\"suggested_materials\":" + materialsJson + ","
           + "\"rationale\":\"先命令后网络\",\"source_mode\":\"partially_grounded\"}";
}

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
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'操作系统规划',"
            "'间隔复习操作系统','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z',"
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
        m_unitOfWork = std::make_unique<Infrastructure::SqlUnitOfWork>(
            DatabaseManager::instance().database());
        return std::make_unique<Application::AiPlanningUseCases>(
            *m_gateway, *aiRepo, *m_retrieval, *m_knowledgeRepo, *m_goalsRepo, *m_melsRepo,
            *m_routesRepo, *m_statesRepo, *m_manifestsRepo, *aiRepo, m_uids, m_clock,
            *m_unitOfWork);
    }

    Domain::Uid goalUid()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='操作系统规划'"));
        q.next();
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    // 建一条已确认路线（单阶段），返回阶段 uid（阶段标题参与知识检索）
    Domain::Uid createConfirmedStage(const QString &title)
    {
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlRouteRepository routesRepo(DatabaseManager::instance().database(),
                                                      m_clock);
        Application::RouteUseCases routeUseCases(routesRepo, goalsRepo, m_uids, m_clock);
        Domain::RouteStage stage;
        stage.uid = m_uids.next();
        stage.title = title.toStdString();
        // 描述与知识库方法文本（"对操作系统知识进行间隔复习的方法"）保持
        // bigram 重叠：检索查询按空格分段的 AND 语义要求每段都有命中
        stage.description =
            QStringLiteral("对操作系统知识做间隔复习").toStdString();
        stage.sequenceNo = 1;
        stage.completionRuleJson = std::string("{}");
        Application::RouteUseCases::ProposeInput input;
        input.goalId = goalUid();
        input.rationale = QStringLiteral("阶段详情测试路线").toStdString();
        input.evidenceSummary = std::string("无");
        input.assumptionsJson = std::string("{}");
        input.stages = {stage};
        input.createdBy = std::string("ai");
        const auto proposed = routeUseCases.proposeRoute(input);
        if (!proposed)
            return {};
        const auto confirmed = routeUseCases.confirmRoute(proposed.value().route.uid, 1);
        if (!confirmed)
            return {};
        QSqlQuery q(DatabaseManager::instance().database());
        q.prepare(QStringLiteral(
            "SELECT uid FROM route_stages_v3 WHERE title=? ORDER BY id DESC LIMIT 1"));
        q.addBindValue(title);
        if (!q.exec() || !q.next())
            return {};
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    // 建一个候选 MEL（任务标题与方法文本 bigram 重叠，保证召回），
    // 返回 (melUid, taskUid)
    std::pair<std::string, std::string> createMelWithSpacedRepetitionTask()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = QStringLiteral(
            "{\"title\":\"本轮复习\",\"tasks\":[{\"title\":\"间隔复习操作系统任务\","
            "\"planned_effort_min\":60,\"required\":true}],\"rationale\":\"容量依据\","
            "\"capacity_min\":120,\"reserve_min\":20,\"period_days\":3,"
            "\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"source_mode\":\"partially_grounded\"}")
                                             .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateMelProposal(userUid, goalUid(), std::nullopt);
        if (!result)
            return {};
        // 进度审查只属于已经由用户确认的执行中 MEL。测试辅助方法把 AI
        // 候选按真实 UI 路径 draft -> awaiting_confirmation -> active。
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::MelUseCases melUseCases(melRepo, m_uids, m_clock);
        const auto parsedMel = Domain::Uid::parse(result.value().aggregateUid);
        if (!parsedMel || !melUseCases.submitForConfirmation(*parsedMel, 1)
            || !melUseCases.confirmAndActivate(*parsedMel, 2))
            return {};
        QSqlQuery q(DatabaseManager::instance().database());
        q.prepare(QStringLiteral(
            "SELECT uid FROM mel_tasks_v4 WHERE mel_id=(SELECT id FROM mels_v4 WHERE uid=?)"));
        q.addBindValue(QString::fromStdString(result.value().aggregateUid));
        if (!q.exec() || !q.next())
            return {};
        return {result.value().aggregateUid, q.value(0).toString().toStdString()};
    }

    std::string methodItemUidText()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral(
            "SELECT uid FROM knowledge_items_v5 WHERE title='间隔复习方法'"));
        if (!q.next())
            return {};
        return q.value(0).toString().toStdString();
    }

    void routeProposalPipeline()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.userText = "路线建议";
        provider.canned.structuredJson = QStringLiteral(
            "{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"stages\":[{\"title\":\"Java 基础\",\"description\":\"掌握语言核心\","
            "\"relationship\":\"start\",\"depends_on\":[]},{\"title\":\"Spring\","
            "\"description\":\"构建后端服务\",\"relationship\":\"after\","
            "\"depends_on\":[\"Java 基础\"]},{\"title\":\"项目实战\","
            "\"description\":\"完成可部署项目\",\"key_contents\":[\"部署\",\"排障\"],"
            "\"relationship\":\"after\",\"depends_on\":[\"Spring\"]}],\"rationale\":\"依据测试\","
            "\"evidence_summary\":\"无\",\"assumptions\":{},\"source_mode\":\"ungrounded\"}")
                                             .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");

        const auto result = pipeline->generateRouteProposal(
            userUid, goalUid(), "我已经掌握 Java 基础，希望数据库与 Web 并行");
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
        QVERIFY2(provider.lastUserPrompt.find("3～5 个粗粒度阶段") != std::string::npos,
                 "route prompt must require a short coarse-grained route");
        QVERIFY2(provider.lastUserPrompt.find("parallel") != std::string::npos,
                 "route prompt must request dependency and parallel semantics");
        QVERIFY2(provider.lastUserPrompt.find("我已经掌握 Java 基础") != std::string::npos,
                 "user guidance must be included in route planning context");
        // 检索命中内容必须进入提示词（标题+摘要，而非仅命中计数）
        QVERIFY2(provider.lastUserPrompt.find("items") != std::string::npos,
                 "knowledge items must be injected into the prompt");
        QVERIFY2(provider.lastUserPrompt.find("间隔复习方法") != std::string::npos,
                 "hit title must be injected into the prompt");

        Infrastructure::SqlRouteRepository routes(DatabaseManager::instance().database(),
                                                  m_clock);
        const auto byGoal = routes.findByGoal(goalUid());
        bool sawDraft = false;
        for (const auto &route : byGoal)
            if (route.status == Domain::RouteStatus::Draft)   // AI 候选（既有约定）
                sawDraft = true;
        QVERIFY(sawDraft);

        QSqlQuery stageFacts(DatabaseManager::instance().database());
        QVERIFY(stageFacts.exec(QStringLiteral(
            "SELECT title, description, completion_rule_json FROM route_stages_v3 "
            "ORDER BY sequence_no")));
        QVERIFY(stageFacts.next());
        QCOMPARE(stageFacts.value(0).toString(), QStringLiteral("Java 基础"));
        QCOMPARE(stageFacts.value(1).toString(), QStringLiteral("掌握语言核心"));
        QVERIFY(stageFacts.value(2).toString().contains(QStringLiteral("start")));
        QVERIFY(stageFacts.next());
        QCOMPARE(stageFacts.value(0).toString(), QStringLiteral("Spring"));
        QVERIFY(stageFacts.value(2).toString().contains(QStringLiteral("Java 基础")));
        QVERIFY(stageFacts.next());
        QCOMPARE(stageFacts.value(0).toString(), QStringLiteral("项目实战"));
        QVERIFY(stageFacts.value(2).toString().contains(QStringLiteral("排障")));

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

    void routeConfirmMarksDecisionAccepted()
    {
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = QStringLiteral(
            "{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"stages\":[{\"title\":\"A\"},{\"title\":\"B\"},{\"title\":\"C\"}],"
            "\"rationale\":\"依据测试\",\"evidence_summary\":\"无\",\"assumptions\":{},"
            "\"source_mode\":\"ungrounded\"}")
                                             .toStdString();
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateRouteProposal(userUid, goalUid());
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        const auto routeUid = *Domain::Uid::parse(result.value().aggregateUid);
        QVERIFY(!routeUid.empty());

        // 决策最初 Pending
        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto before = ai.listDecisions("route_proposal", 10);
        QVERIFY(!before.empty());
        QVERIFY(before.front().userStatus == Domain::DecisionUserStatus::Pending);

        // 用户确认路线 → 决策 accepted + selected_json 记录确认采用的内容
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlRouteRepository routesRepo(DatabaseManager::instance().database(),
                                                      m_clock);
        Application::RouteUseCases routeUseCases(routesRepo, goalsRepo, m_uids, m_clock, &ai);
        const auto confirmed = routeUseCases.confirmRoute(routeUid, 1);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));
        const auto after = ai.findDecision(before.front().uid);
        QVERIFY(after);
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);
        QVERIFY(after->selectedJson && !after->selectedJson->empty());

        // nullptr 决策库路径兼容（历史调用点）：已确认路线重复确认 → Conflict
        //（状态门禁先于决策标记，不因无决策库而崩溃或伪造成功）
        Application::RouteUseCases legacyCompatible(routesRepo, goalsRepo, m_uids, m_clock);
        const auto repeated = legacyCompatible.confirmRoute(routeUid, 2);
        QVERIFY(!repeated);
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

        // 候选方法内容(标题/摘要)必须注入提示词(此前只传版本 uid,
        // 模型看不到方法内容——与检索计数同族缺陷)
        QVERIFY2(provider.lastUserPrompt.find("间隔复习方法") != std::string::npos,
                 "method content must be injected into the suggestion prompt");

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

        // 物理删除（用户决策）：决策记录与底层任务/调用一起删除
        const std::string decisionUid = result.value().decisionUid;
        const std::string jobUid = [&] {
            QSqlQuery q(DatabaseManager::instance().database());
            q.exec(QStringLiteral("SELECT job_uid FROM decision_records_v6 WHERE uid='%1'")
                       .arg(QString::fromStdString(decisionUid)));
            q.next();
            return q.value(0).toString().toStdString();
        }();
        QVERIFY(!jobUid.empty());
        const auto removed =
            ai.deleteDecision(*Domain::Uid::parse(decisionUid));
        QVERIFY(removed.ok);
        QVERIFY(!ai.findDecision(*Domain::Uid::parse(decisionUid)));
        QSqlQuery jobCount(DatabaseManager::instance().database());
        jobCount.prepare(QStringLiteral("SELECT COUNT(*) FROM ai_jobs_v6 WHERE uid=?"));
        jobCount.addBindValue(QString::fromStdString(jobUid));
        QVERIFY(jobCount.exec() && jobCount.next());
        QCOMPARE(jobCount.value(0).toInt(), 0);
    }

    void progressReviewPipelineAndAdopt()
    {
        const auto melAndTask = createMelWithSpacedRepetitionTask();
        QVERIFY(!melAndTask.first.empty() && !melAndTask.second.empty());
        const auto melUid = *Domain::Uid::parse(melAndTask.first);
        Q_UNUSED(melUid)

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            std::string("{\"observations\":[\"任务完成过半\"],\"task_updates\":[")
            + "{\"task_uid\":\"" + melAndTask.second
            + "\",\"progress\":0.5,\"state\":\"active\",\"actual_minutes\":30,"
              "\"rationale\":\"用户报告进展顺利\"}],\"mel_progress\":0.5,"
              "\"learned_contents\":[\"间隔复习\"],\"next_action\":\"完成剩余练习\","
              "\"execution_complete\":false,\"user_text\":\"接下来建议…\","
              "\"source_mode\":\"partially_grounded\"}";
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->reviewProgress(userUid, melUid,
                                                     "间隔复习进展顺利");
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QVERIFY2(provider.lastUserPrompt.find("间隔复习进展顺利") != std::string::npos,
                 "report text must be injected into the prompt");
        QVERIFY2(provider.lastUserPrompt.find(melAndTask.second) != std::string::npos,
                 "task uid list must be injected into the prompt");

        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto decision = ai.findDecision(
            *Domain::Uid::parse(result.value().decisionUid));
        QVERIFY(decision);
        QVERIFY(decision->userStatus == Domain::DecisionUserStatus::Pending);
        QCOMPARE(decision->aggregateType, std::string("mel"));

        // 确认后才落地任务进度
        const auto applied = pipeline->adoptProgressReview(decision->uid);
        if (!applied)
            QFAIL(qPrintable(QString::fromStdString(applied.error().message + ": "
                                                    + applied.error().detail)));
        QCOMPARE(applied.value(), 1);
        const auto after = ai.findDecision(decision->uid);
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);
        const auto duplicateAdopt = pipeline->adoptProgressReview(decision->uid);
        QVERIFY(!duplicateAdopt);
        QVERIFY(duplicateAdopt.error().code == Application::ErrorCode::Conflict);

        QSqlQuery taskQ(DatabaseManager::instance().database());
        taskQ.prepare(QStringLiteral(
            "SELECT progress FROM mel_tasks_v4 WHERE uid=?"));
        taskQ.addBindValue(QString::fromStdString(melAndTask.second));
        QVERIFY(taskQ.exec() && taskQ.next());
        QCOMPARE(taskQ.value(0).toDouble(), 0.5);
    }

    void progressReviewRejectsFabricatedMethod()
    {
        const auto melAndTask = createMelWithSpacedRepetitionTask();
        QVERIFY(!melAndTask.first.empty() && !melAndTask.second.empty());
        const auto melUid = *Domain::Uid::parse(melAndTask.first);

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            "{\"observations\":[\"x\"],\"task_updates\":[{\"task_uid\":"
            "\"ffffffff-ffff-ffff-ffff-ffffffffffff\",\"progress\":1,"
            "\"state\":\"completed\",\"actual_minutes\":0,\"rationale\":\"x\"}],"
            "\"mel_progress\":1,\"learned_contents\":[],\"next_action\":\"x\","
            "\"execution_complete\":true,\"user_text\":\"y\",\"source_mode\":\"ungrounded\"}";
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->reviewProgress(userUid, melUid, "间隔复习进展顺利");
        QVERIFY(!result);
        QVERIFY(result.error().code == Application::ErrorCode::Validation);
    }

    void progressReviewRejectsNonExecutableMel()
    {
        const auto melAndTask = createMelWithSpacedRepetitionTask();
        QVERIFY(!melAndTask.first.empty());
        QSqlQuery state(DatabaseManager::instance().database());
        state.prepare(QStringLiteral(
            "UPDATE mels_v4 SET state='execution_complete' WHERE uid=?"));
        state.addBindValue(QString::fromStdString(melAndTask.first));
        QVERIFY(state.exec());

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            std::string("{\"observations\":[\"x\"],\"adjustment_suggestions\":[")
            + "{\"type\":\"note\",\"title\":\"x\",\"detail\":\"x\"}],"
              "\"user_text\":\"y\",\"source_mode\":\"ungrounded\"}";
        const auto pipeline = makePipeline(provider);
        const auto userUid =
            *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->reviewProgress(
            userUid, *Domain::Uid::parse(melAndTask.first), "已经执行完成");
        QVERIFY(!result);
        QVERIFY(result.error().code == Application::ErrorCode::Conflict);
        QVERIFY2(provider.lastUserPrompt.empty(),
                 "provider must not run for a non-executable mel");
    }

    void conflictFlagPropagates()
    {
        // 给检索召回的方法当前版本挂一条 contradicts 证据链路
        // （paper analysis 管线真实写入的形态；此处直接 SQL 造数）
        Infrastructure::SqlKnowledgeRepository knowledgeRepo(
            DatabaseManager::instance().database(), m_clock);
        const auto methodItem = knowledgeRepo.findItem(
            *Domain::Uid::parse(methodItemUidText()));
        QVERIFY(methodItem && methodItem->currentVersionUid);
        const std::string versionUid = *methodItem->currentVersionUid;

        QSqlQuery q(DatabaseManager::instance().database());
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO source_records_v5(uid,source_type,title,accessed_at,metadata_json,"
            "content_hash,trust_tier,created_at,updated_at) VALUES("
            "'00000000-0000-0000-0000-0000000000cf','manual','冲突证据来源',"
            "'2026-09-30T00:00:00Z','{}','conflict:src:1','vetted',"
            "'2026-09-30T00:00:00Z','2026-09-30T00:00:00Z')")));
        QVERIFY(q.exec(QStringLiteral(
            "INSERT INTO evidence_fragments_v5(uid,source_id,locator_json,text,fragment_hash,"
            "created_at) VALUES('00000000-0000-0000-0000-0000000000d0',"
            "(SELECT id FROM source_records_v5 WHERE "
            "uid='00000000-0000-0000-0000-0000000000cf'),"
            "'{}','间隔复习对短期记忆可能有反作用','conflict:frag:1',"
            "'2026-09-30T00:00:00Z')")));
        QSqlQuery link(DatabaseManager::instance().database());
        link.prepare(QStringLiteral(
            "INSERT INTO evidence_links_v5(knowledge_version_id,fragment_id,relation,"
            "strength,note) VALUES("
            "(SELECT id FROM knowledge_versions_v5 WHERE uid=?),"
            "(SELECT id FROM evidence_fragments_v5 WHERE "
            "uid='00000000-0000-0000-0000-0000000000d0'),"
            "'contradicts',0.8,'反向证据')"));
        link.addBindValue(QString::fromStdString(versionUid));
        QVERIFY(link.exec());

        // 咨询：命中含冲突证据的条目 → 决策 warningJson 置位、提示词带 _conflict
        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson =
            std::string("{\"user_text\":\"这是回答\",\"source_mode\":\"ungrounded\"}");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto advisor = pipeline->askAdvisor(userUid, "怎么复习操作系统？");
        if (!advisor)
            QFAIL(qPrintable(QString::fromStdString(advisor.error().message + ": "
                                                    + advisor.error().detail)));
        QVERIFY2(provider.lastUserPrompt.find("\"_conflict\":true") != std::string::npos,
                 "prompt must carry the conflict flag");
        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto decision = ai.findDecision(
            *Domain::Uid::parse(advisor.value().decisionUid));
        QVERIFY(decision);
        QCOMPARE(decision->warningJson, std::string("{\"conflict\":true}"));

        // 路线候选同样传播
        FakeProvider routeProvider;
        routeProvider.canned.ok = true;
        routeProvider.canned.structuredJson = QStringLiteral(
            "{\"goal_uid\":\"00000000-0000-0000-0000-0000000000ca\","
            "\"stages\":[{\"title\":\"A\"},{\"title\":\"B\"},{\"title\":\"C\"}],"
            "\"rationale\":\"依据测试\",\"evidence_summary\":\"无\",\"assumptions\":{},"
            "\"source_mode\":\"ungrounded\"}")
                                                   .toStdString();
        const auto routePipeline = makePipeline(routeProvider);
        const auto route = routePipeline->generateRouteProposal(userUid, goalUid());
        if (!route)
            QFAIL(qPrintable(QString::fromStdString(route.error().message + ": "
                                                    + route.error().detail)));
        const auto routeDecision = ai.findDecision(
            *Domain::Uid::parse(route.value().decisionUid));
        QVERIFY(routeDecision);
        QCOMPARE(routeDecision->warningJson, std::string("{\"conflict\":true}"));
    }

    void stageDetailPipeline()
    {
        const auto stageUid = createConfirmedStage(QStringLiteral("间隔复习操作系统基础"));
        QVERIFY(!stageUid.empty());
        const std::string itemUid = methodItemUidText();
        QVERIFY(!itemUid.empty());

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.userText = "阶段详情建议";
        provider.canned.structuredJson =
            stageDetailJson(stageUid.value(),
                            "[{\"item_uid\":\"" + itemUid
                                + "\",\"reason\":\"覆盖阶段核心操作\"}]");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result =
            pipeline->generateStageDetail(userUid, stageUid, "请把项目练习提前");
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        QCOMPARE(result.value().aggregateUid, stageUid.value());
        QVERIFY(!result.value().decisionUid.empty());

        // 提示词：阶段信息、契约说明（可验证/完成标准）、候选资料 uid、用户意见
        QVERIFY2(provider.lastUserPrompt.find("间隔复习操作系统基础") != std::string::npos,
                 "stage title must be injected into the prompt");
        QVERIFY2(provider.lastUserPrompt.find("可验证") != std::string::npos,
                 "stage contract hint must be injected into the prompt");
        QVERIFY2(provider.lastUserPrompt.find(itemUid) != std::string::npos,
                 "material candidate uid must be injected into the prompt");
        QVERIFY2(provider.lastUserPrompt.find("请把项目练习提前") != std::string::npos,
                 "user guidance must be injected into the prompt");

        // 详情 v1 落库（ai，未确认）；资料行 pending 且版本为方法当前版本
        QSqlQuery detail(DatabaseManager::instance().database());
        QVERIFY(detail.exec(QStringLiteral(
            "SELECT version_no, created_by, user_confirmed_at FROM route_stage_details_v11 "
            "ORDER BY id DESC LIMIT 1")));
        QVERIFY(detail.next());
        QCOMPARE(detail.value(0).toInt(), 1);
        QCOMPARE(detail.value(1).toString(), QStringLiteral("ai"));
        QVERIFY(detail.value(2).isNull());

        QSqlQuery material(DatabaseManager::instance().database());
        material.prepare(QStringLiteral(
            "SELECT knowledge_version_uid, user_choice FROM route_stage_materials_v11 "
            "WHERE knowledge_item_uid=?"));
        material.addBindValue(QString::fromStdString(itemUid));
        QVERIFY(material.exec() && material.next());
        QCOMPARE(material.value(0).toString(), QString::fromStdString(m_methodVersionUid));
        QCOMPARE(material.value(1).toString(), QStringLiteral("pending"));

        // 决策记录 Pending（route_stage 聚合），知识快照落库
        Infrastructure::SqlAiRepository ai(DatabaseManager::instance().database(), m_clock);
        const auto decisions = ai.listDecisions("stage_detail", 10);
        QVERIFY(!decisions.empty());
        const auto &decision = decisions.front();
        QVERIFY(decision.userStatus == Domain::DecisionUserStatus::Pending);
        QCOMPARE(decision.aggregateType, std::string("route_stage"));
        QCOMPARE(decision.aggregateUid, stageUid.value());

        // 整版确认链路：确认后决策 accepted、确认时间落库
        Infrastructure::SqlRouteRepository routes(DatabaseManager::instance().database(),
                                                  m_clock);
        Application::RouteStageDetailUseCases detailUseCases(routes, ai, m_clock);
        const auto confirmed = detailUseCases.confirmStageDetail(stageUid, 1);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));
        const auto after = ai.findDecision(decision.uid);
        QVERIFY(after);
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);
        QVERIFY(detail.exec(QStringLiteral(
            "SELECT user_confirmed_at FROM route_stage_details_v11 ORDER BY id DESC LIMIT 1")));
        QVERIFY(detail.next());
        QVERIFY(!detail.value(0).isNull());
    }

    void stageDetailMaterialSubsetViolation()
    {
        const auto stageUid = createConfirmedStage(QStringLiteral("间隔复习操作系统进阶"));
        QVERIFY(!stageUid.empty());

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = stageDetailJson(
            stageUid.value(),
            "[{\"item_uid\":\"ffffffff-ffff-ffff-ffff-ffffffffffff\",\"reason\":\"凭空引用\"}]");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateStageDetail(userUid, stageUid);
        QVERIFY(!result);
        QVERIFY(result.error().code == Application::ErrorCode::Validation);

        // 检索确实召回了方法（候选集非空），拒绝来自"凭空引用"而非"零召回"
        QVERIFY2(provider.lastUserPrompt.find(methodItemUidText()) != std::string::npos,
                 "retrieved material candidate must be injected into the prompt");

        // 零写入：详情与资料都没有落库
        QSqlQuery detail(DatabaseManager::instance().database());
        detail.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM route_stage_details_v11 WHERE stage_id="
            "(SELECT id FROM route_stages_v3 WHERE uid=?)"));
        detail.addBindValue(QString::fromStdString(stageUid.value()));
        QVERIFY(detail.exec() && detail.next());
        QCOMPARE(detail.value(0).toInt(), 0);
        QSqlQuery material(DatabaseManager::instance().database());
        material.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM route_stage_materials_v11 WHERE stage_id="
            "(SELECT id FROM route_stages_v3 WHERE uid=?)"));
        material.addBindValue(QString::fromStdString(stageUid.value()));
        QVERIFY(material.exec() && material.next());
        QCOMPARE(material.value(0).toInt(), 0);
    }

    void stageDetailRequiresConfirmedRoute()
    {
        // draft 路线（不确认）
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlRouteRepository routesRepo(DatabaseManager::instance().database(),
                                                      m_clock);
        Application::RouteUseCases routeUseCases(routesRepo, goalsRepo, m_uids, m_clock);
        Domain::RouteStage stage;
        stage.uid = m_uids.next();
        stage.title = QStringLiteral("未确认阶段").toStdString();
        stage.sequenceNo = 1;
        stage.completionRuleJson = std::string("{}");
        Application::RouteUseCases::ProposeInput input;
        input.goalId = goalUid();
        input.rationale = QStringLiteral("未确认路线").toStdString();
        input.evidenceSummary = std::string("无");
        input.assumptionsJson = std::string("{}");
        input.stages = {stage};
        input.createdBy = std::string("ai");
        QVERIFY(routeUseCases.proposeRoute(input));
        QSqlQuery stageQ(DatabaseManager::instance().database());
        QVERIFY(stageQ.exec(QStringLiteral(
            "SELECT uid FROM route_stages_v3 WHERE title='未确认阶段'")));
        QVERIFY(stageQ.next());
        const auto stageUid = *Domain::Uid::parse(stageQ.value(0).toString().toStdString());

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = stageDetailJson(stageUid.value(), "[]");
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateStageDetail(userUid, stageUid);
        QVERIFY(!result);
        QVERIFY(result.error().code == Application::ErrorCode::Conflict);
        QVERIFY2(provider.lastUserPrompt.empty(), "provider must not be called for unconfirmed route");
    }

    void stageDetailContractFailureWritesNothing()
    {
        const auto stageUid = createConfirmedStage(QStringLiteral("间隔复习操作系统部署"));
        QVERIFY(!stageUid.empty());

        FakeProvider provider;
        provider.canned.ok = true;
        provider.canned.structuredJson = "{\"stage_uid\":\"" + stageUid.value() + "\"}";
        const auto pipeline = makePipeline(provider);
        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto result = pipeline->generateStageDetail(userUid, stageUid);
        QVERIFY(!result);
        // 网关内契约校验失败按可重试处理（attempt<maxAttempts）→
        // ExternalUnavailable；runJob 层直接校验失败则为 Validation（既有
        // contractFailureWritesNothing 同款口径）
        QVERIFY(result.error().code == Application::ErrorCode::Validation
                || result.error().code == Application::ErrorCode::ExternalUnavailable);

        QSqlQuery detail(DatabaseManager::instance().database());
        detail.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM route_stage_details_v11 WHERE stage_id="
            "(SELECT id FROM route_stages_v3 WHERE uid=?)"));
        detail.addBindValue(QString::fromStdString(stageUid.value()));
        QVERIFY(detail.exec() && detail.next());
        QCOMPARE(detail.value(0).toInt(), 0);
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
    std::unique_ptr<Infrastructure::SqlUnitOfWork> m_unitOfWork;
};

QTEST_GUILESS_MAIN(TstAiPlanning)
#include "tst_ai_planning.moc"
