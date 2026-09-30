// 路线阶段详情（迁移 v11 / DD-001 补充）：详情版本只追加、资料绑定 upsert 语义、
// 确认门禁与资料选择。
// 说明：真实 SQLite 临时库 + 真实 Sql 仓储；审计 sink 在 initTestCase 注册
// （用例层的确认/资料决定写审计事件，未注册时静默 no-op 会误判）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/audit/Audit.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/route/RouteStageDetailUseCases.h"
#include "application/usecases/route/RouteUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/ai/Ai.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/SqlAuditRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kGoalTitle = "Linux 运维路线目标";
const char *kItemUidA = "item-stage-material-aaa";
const char *kItemUidB = "item-stage-material-bbb";
} // namespace

class TstStageDetail : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        try {
            initTestCaseBody();
        } catch (const std::exception &e) {
            QFAIL(qPrintable(QStringLiteral("exception: %1").arg(e.what())));
        }
    }

private:
    void initTestCaseBody()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_stage_detail.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));

        // 领域清单 + 用户档案
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

        // 目标 → 路线（1 个阶段）→ 用户确认
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const auto manifestUid =
            *Domain::Uid::parse(manifest.value(0).toString().toStdString());
        const auto userUid = *Domain::Uid::parse(kUserUid);

        auto goalsRepo = goals();
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = userUid;
        goalInput.domainManifestId = manifestUid;
        goalInput.title = kGoalTitle;
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        const auto goalResult = goalUseCases.createGoal(goalInput);
        if (!goalResult)
            QFAIL(qPrintable(QString::fromStdString(goalResult.error().message + ": "
                                                    + goalResult.error().detail)));

        Domain::RouteStage stage;
        stage.uid = m_uids.next();
        stage.title = QStringLiteral("Linux 基础与网络").toStdString();
        stage.description =
            QStringLiteral("掌握命令行、文件与进程、网络基础，为服务器运维打底").toStdString();
        stage.sequenceNo = 1;
        stage.completionRuleJson = std::string("{}");
        stage.estimatedEffortMin = 600;

        auto routesRepo = routes();
        Application::RouteUseCases routeUseCases(routesRepo, goalsRepo, m_uids, m_clock);
        Application::RouteUseCases::ProposeInput routeInput;
        routeInput.goalId = goalResult.value().goal.uid;
        routeInput.rationale = QStringLiteral("先基础后进阶再实战").toStdString();
        routeInput.evidenceSummary = QStringLiteral("依据：领域知识检索（候选）").toStdString();
        routeInput.assumptionsJson = std::string("{}");
        routeInput.stages = {stage};
        routeInput.createdBy = std::string("ai");
        const auto proposed = routeUseCases.proposeRoute(routeInput);
        if (!proposed)
            QFAIL(qPrintable(QString::fromStdString(proposed.error().message + ": "
                                                    + proposed.error().detail)));
        const auto confirmed = routeUseCases.confirmRoute(proposed.value().route.uid, 1);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));

        QSqlQuery stageQuery(DatabaseManager::instance().database());
        QVERIFY(stageQuery.exec(QStringLiteral("SELECT uid FROM route_stages_v3 LIMIT 1")));
        QVERIFY(stageQuery.next());
        m_stageUidText = stageQuery.value(0).toString();

        // 审计 sink：用例层确认/资料决定写审计事件（组合根/测试均可注册）
        m_audit = std::make_unique<Infrastructure::SqlAuditRepository>(
            DatabaseManager::instance().database(), m_clock, m_uids);
        Application::Audit::setSink(m_audit.get());
    }

private slots:
    void migrationV11TablesExist()
    {
        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='route_stage_details_v11'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='route_stage_materials_v11'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_route_stage_details_v11_stage'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name='idx_route_stage_materials_v11_stage'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toInt(), 1);
    }

    void insertStageDetailAppendsVersions()
    {
        const auto stageUid = *Domain::Uid::parse(m_stageUidText.toStdString());
        auto repo = routes();

        // locateStage：阶段联回所属版本与路线（路线已确认）
        const auto location = repo.locateStage(stageUid);
        QVERIFY(location.has_value());
        QCOMPARE(location->stage.title, std::string("Linux 基础与网络"));
        QCOMPARE(location->routeVersionNo, 1);
        QVERIFY(location->routeStatus == Domain::RouteStatus::Confirmed);

        // v1：AI 候选（未确认）
        Domain::StageDetailVersion v1;
        v1.stageUid = stageUid;
        v1.versionNo = 1;
        v1.outcomesJson =
            std::string(R"([{"description":"能在服务器上独立完成文件、权限与进程管理"}])");
        v1.tasksJson =
            std::string(R"([{"sequence_no":1,"title":"命令行与文件系统","description":"常用命令与目录结构"},{"sequence_no":2,"title":"进程与服务","description":"进程查看、信号与 systemd 基础"}])");
        v1.projectsJson = std::string("[]");
        v1.criteriaJson = std::string(R"([{"description":"不看资料完成 10 个常用操作题"}])");
        v1.rationale = QStringLiteral("从命令行到进程再到网络，按依赖顺序").toStdString();
        v1.createdBy = std::string("ai");
        const auto insertedV1 = repo.insertStageDetail(v1);
        QVERIFY(insertedV1);
        QVERIFY(!insertedV1.value().empty());

        // v2：调整后的新版本；空 JSON 默认落 '[]'
        Domain::StageDetailVersion v2 = v1;
        v2.versionNo = 2;
        v2.outcomesJson.clear();
        v2.tasksJson.clear();
        v2.projectsJson.clear();
        v2.criteriaJson.clear();
        v2.rationale = QStringLiteral("按用户意见把项目练习前移").toStdString();
        const auto insertedV2 = repo.insertStageDetail(v2);
        QVERIFY(insertedV2);
        QVERIFY(insertedV2.value() != insertedV1.value());

        const auto versions = repo.stageDetailVersionsOf(stageUid);
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions[0].versionNo, 1);
        QCOMPARE(versions[1].versionNo, 2);
        QVERIFY(!versions[0].userConfirmedAt.has_value());
        QCOMPARE(versions[1].outcomesJson, std::string("[]"));
        QCOMPARE(versions[1].tasksJson, std::string("[]"));

        // 重复 version_no → Storage
        Domain::StageDetailVersion dup = v1;
        dup.versionNo = 2;
        const auto dupResult = repo.insertStageDetail(dup);
        QVERIFY(!dupResult);
        QVERIFY(dupResult.error().code == Application::ErrorCode::Storage);

        // 非法 JSON → Validation
        Domain::StageDetailVersion bad = v1;
        bad.versionNo = 3;
        bad.tasksJson = std::string("{oops");
        const auto badResult = repo.insertStageDetail(bad);
        QVERIFY(!badResult);
        QVERIFY(badResult.error().code == Application::ErrorCode::Validation);
    }

    void materialUpsertKeepsAccepted()
    {
        const auto stageUid = *Domain::Uid::parse(m_stageUidText.toStdString());
        auto repo = routes();

        Domain::StageMaterialBinding materialA;
        materialA.stageUid = stageUid;
        materialA.knowledgeItemUid = kItemUidA;
        materialA.knowledgeVersionUid = "ver-1";
        materialA.rank = 0;
        materialA.reason = QStringLiteral("覆盖阶段核心操作").toStdString();
        materialA.userChoice = "pending";
        materialA.createdBy = "ai";
        QVERIFY(repo.upsertStageMaterial(materialA).ok);

        // 用户接受
        QVERIFY(repo.updateStageMaterialChoice(stageUid, kItemUidA, "accepted").ok);

        // 重新建议：内容更新，但已 accepted 的用户决定保持不动
        Domain::StageMaterialBinding resuggestA = materialA;
        resuggestA.knowledgeVersionUid = "ver-2";
        resuggestA.rank = 3;
        resuggestA.reason = QStringLiteral("重新生成后的新理由").toStdString();
        QVERIFY(repo.upsertStageMaterial(resuggestA).ok);

        auto materials = repo.stageMaterialsOf(stageUid);
        QCOMPARE(materials.size(), 1);
        QCOMPARE(materials[0].userChoice, std::string("accepted"));
        QCOMPARE(materials[0].knowledgeVersionUid, std::string("ver-2"));
        QCOMPARE(materials[0].rank, 3);
        QCOMPARE(materials[0].reason, QStringLiteral("重新生成后的新理由").toStdString());

        // 新条目：pending 行重建议保持 pending 并更新内容
        Domain::StageMaterialBinding materialB = materialA;
        materialB.knowledgeItemUid = kItemUidB;
        materialB.knowledgeVersionUid = "ver-b1";
        materialB.rank = 0;
        materialB.userChoice = "pending";
        QVERIFY(repo.upsertStageMaterial(materialB).ok);
        materialB.knowledgeVersionUid = "ver-b2";
        materialB.rank = 1;
        QVERIFY(repo.upsertStageMaterial(materialB).ok);

        materials = repo.stageMaterialsOf(stageUid);
        QCOMPARE(materials.size(), 2);
        const auto findB = [&materials]() -> const Domain::StageMaterialBinding * {
            for (const auto &m : materials)
                if (m.knowledgeItemUid == kItemUidB)
                    return &m;
            return nullptr;
        };
        QVERIFY(findB() != nullptr);
        QCOMPARE(findB()->userChoice, std::string("pending"));
        QCOMPARE(findB()->knowledgeVersionUid, std::string("ver-b2"));
        QCOMPARE(findB()->rank, 1);

        // 拒绝与不存在的条目
        QVERIFY(repo.updateStageMaterialChoice(stageUid, kItemUidB, "rejected").ok);
        const auto missing = repo.updateStageMaterialChoice(stageUid, "item-not-exist", "accepted");
        QVERIFY(!missing.ok);
        QVERIFY(missing.error.code == Application::ErrorCode::NotFound);
    }

    void confirmStageDetailGates()
    {
        const auto stageUid = *Domain::Uid::parse(m_stageUidText.toStdString());
        auto repo = routes();
        Infrastructure::SqlAiRepository decisions(DatabaseManager::instance().database(),
                                                  m_clock);
        Application::RouteStageDetailUseCases useCases(repo, decisions, m_clock);

        // 模拟 AI 管线产物：一条 Pending 阶段详情决策
        Domain::DecisionRecord decision;
        decision.uid = m_uids.next();
        decision.decisionType = "stage_detail";
        decision.aggregateType = "route_stage";
        decision.aggregateUid = stageUid.value();
        decision.inputSnapshotJson = std::string("{}");
        decision.candidateJson = std::string("{}");
        decision.rationale = QStringLiteral("测试依据").toStdString();
        decision.sourceMode = Domain::SourceMode::Ungrounded;
        decision.userStatus = Domain::DecisionUserStatus::Pending;
        decision.createdAt = m_clock.utcIso();
        QVERIFY(decisions.insertDecision(decision).ok);

        // 未确认路线的阶段 → Conflict（新建 draft 路线不确认）
        {
            Domain::RouteStage draftStage;
            draftStage.uid = m_uids.next();
            draftStage.title = QStringLiteral("未确认阶段").toStdString();
            draftStage.sequenceNo = 1;
            draftStage.completionRuleJson = std::string("{}");
            auto goalsRepo = goals();
            Application::RouteUseCases routeUseCases(repo, goalsRepo, m_uids, m_clock);
            Application::RouteUseCases::ProposeInput input;
            input.goalId = *Domain::Uid::parse(goalUidText(kGoalTitle).toStdString());
            input.rationale = QStringLiteral("未确认路线").toStdString();
            input.evidenceSummary = std::string("无");
            input.assumptionsJson = std::string("{}");
            input.stages = {draftStage};
            input.createdBy = std::string("ai");
            QVERIFY(routeUseCases.proposeRoute(input));
            QSqlQuery draftStageQ(DatabaseManager::instance().database());
            QVERIFY(draftStageQ.exec(QStringLiteral(
                "SELECT uid FROM route_stages_v3 WHERE title='未确认阶段'")));
            QVERIFY(draftStageQ.next());
            const auto draftStageUid =
                *Domain::Uid::parse(draftStageQ.value(0).toString().toStdString());
            const auto rejected = useCases.confirmStageDetail(draftStageUid, 1);
            QVERIFY(!rejected);
            QVERIFY(rejected.error().code == Application::ErrorCode::Conflict);
        }

        // 期望版本不是最新（案例 2 已写入 v1/v2）→ Conflict（防陈旧确认）
        const auto stale = useCases.confirmStageDetail(stageUid, 1);
        QVERIFY(!stale);
        QVERIFY(stale.error().code == Application::ErrorCode::Conflict);

        // 确认最新版 → 成功：确认时间落库 + 决策 accepted + 审计
        const auto confirmed = useCases.confirmStageDetail(stageUid, 2);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));
        const auto versions = repo.stageDetailVersionsOf(stageUid);
        QVERIFY(versions.back().userConfirmedAt.has_value());
        const auto after = decisions.findDecision(decision.uid);
        QVERIFY(after);
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);

        // 重复确认 → Conflict（不可覆盖的历史事实）
        const auto repeated = useCases.confirmStageDetail(stageUid, 2);
        QVERIFY(!repeated);
        QVERIFY(repeated.error().code == Application::ErrorCode::Conflict);

        bool sawConfirmAudit = false;
        for (const auto &event : m_audit->eventsOf("route_stage", stageUid.value(), 20))
            if (event.action == "route.stage_detail_confirmed")
                sawConfirmAudit = true;
        QVERIFY(sawConfirmAudit);
    }

    void respondStageMaterialTransitions()
    {
        const auto stageUid = *Domain::Uid::parse(m_stageUidText.toStdString());
        auto repo = routes();
        Infrastructure::SqlAiRepository decisions(DatabaseManager::instance().database(),
                                                  m_clock);
        Application::RouteStageDetailUseCases useCases(repo, decisions, m_clock);

        // rejected → accepted（允许改主意；案例 3 结束时 bbb 是 rejected）
        const auto accepted = useCases.respondStageMaterial(stageUid, kItemUidB, "accepted");
        if (!accepted)
            QFAIL(qPrintable(QString::fromStdString(accepted.error().message + ": "
                                                    + accepted.error().detail)));
        const auto materials = repo.stageMaterialsOf(stageUid);
        const Domain::StageMaterialBinding *foundB = nullptr;
        for (const auto &material : materials)
            if (material.knowledgeItemUid == kItemUidB)
                foundB = &material;
        QVERIFY(foundB != nullptr);
        QCOMPARE(foundB->userChoice, std::string("accepted"));

        // 非法选择 → Validation
        const auto invalid = useCases.respondStageMaterial(stageUid, kItemUidB, "tried");
        QVERIFY(!invalid);
        QVERIFY(invalid.error().code == Application::ErrorCode::Validation);

        // 不存在的条目 → NotFound（资料只能来自 AI 建议，不在此插入）
        const auto missing = useCases.respondStageMaterial(stageUid, "item-not-exist", "accepted");
        QVERIFY(!missing);
        QVERIFY(missing.error().code == Application::ErrorCode::NotFound);

        // 审计事件
        bool sawResponded = false;
        for (const auto &event : m_audit->eventsOf("route_stage", stageUid.value(), 20))
            if (event.action == "route.stage_material_responded")
                sawResponded = true;
        QVERIFY(sawResponded);
    }

private:
    // 仓库按需构造：必须在 DatabaseManager::open() 之后获取连接句柄。
    Infrastructure::SqlGoalRepository goals()
    {
        return Infrastructure::SqlGoalRepository(DatabaseManager::instance().database(), m_clock);
    }

    Infrastructure::SqlRouteRepository routes()
    {
        return Infrastructure::SqlRouteRepository(DatabaseManager::instance().database(), m_clock);
    }

    QString goalUidText(const char *title)
    {
        QSqlQuery query(DatabaseManager::instance().database());
        query.prepare(QStringLiteral("SELECT uid FROM goals_v3 WHERE title=?"));
        query.addBindValue(QString::fromUtf8(title));
        if (!query.exec() || !query.next())
            return {};
        return query.value(0).toString();
    }

    QString m_path;
    QString m_stageUidText;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    std::unique_ptr<Infrastructure::SqlAuditRepository> m_audit;
};

QTEST_GUILESS_MAIN(TstStageDetail)
#include "tst_stage_detail.moc"
