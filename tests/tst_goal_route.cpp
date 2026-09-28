// IMP-002：Goal/Route 用例与领域配置（DD-001 §3/§5；DB-05 并发冲突）
// 覆盖：目标创建/层级/环检测/乐观并发、路线候选/用户确认/版本保留、
//       学习领域清单种子（参数不写死）、旧目标只读投影。
// 说明：QtTest 每个测试函数使用新实例，共享数据在 initTestCase 建立，
//       各函数通过标题查询 UID。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/route/RouteUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/LegacyGoalReader.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kRootTitle = "计算机专业基础";
const char *kChildTitle = "操作系统";
} // namespace

class TstGoalRoute : public QObject
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
    // initTestCaseBody 是普通私有方法（非 slot）：QtTest 会把 private slots 全部
    // 注册为测试函数，若把初始化逻辑放在 slot 中会被执行两次。
    void initTestCaseBody()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_goal_route.db"));
        // 注意：本函数不得声明为 private slot——QtTest 会把所有 private slot
        // 当作测试函数执行，导致初始化逻辑重复运行。
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

        const auto manifestUid = Domain::Uid::parse(manifestUidText().toStdString());
        const auto userUid = Domain::Uid::parse(kUserUid);
        QVERIFY(manifestUid && userUid);

        auto goalsRepo = goals();
        Application::GoalUseCases useCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput rootInput;
        rootInput.userId = *userUid;
        rootInput.domainManifestId = *manifestUid;
        rootInput.title = QString::fromUtf8(kRootTitle).toStdString();
        rootInput.desiredLevelJson =
            QStringLiteral("{\"criterion\":\"能独立完成课程要求的核心问题\"}").toStdString();
        rootInput.userDefinedLevel = true;
        const auto rootResult = useCases.createGoal(rootInput);
        if (!rootResult)
            QFAIL(qPrintable(QString::fromStdString(rootResult.error().message + ": "
                                                    + rootResult.error().detail)));
        QCOMPARE(rootResult.value().goal.revision, 1);

        Application::GoalUseCases::CreateInput childInput = rootInput;
        childInput.title = QString::fromUtf8(kChildTitle).toStdString();
        childInput.parentGoalId = rootResult.value().goal.uid;
        const auto childResult = useCases.createGoal(childInput);
        if (!childResult)
            QFAIL(qPrintable(QString::fromStdString(childResult.error().message + ": "
                                                    + childResult.error().detail)));
    }

private slots:
    void manifestSeedIsIdempotentAndParameterNotHardcoded()
    {
        Infrastructure::LearningManifestSeed seed(DatabaseManager::instance().database(),
                                                  m_clock);
        QVERIFY(seed.ensureSeeded()); // 幂等：initTestCase 已种过一次

        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(query.exec(QStringLiteral(
            "SELECT manifest_json FROM domain_manifest_versions_v3 WHERE version_no=1")));
        QVERIFY(query.next());
        const QJsonObject manifest =
            QJsonDocument::fromJson(query.value(0).toString().toUtf8()).object();
        QCOMPARE(manifest.value("domain_id").toString(), QStringLiteral("learning"));
        const auto parameters = manifest.value("parameter_specs").toArray();
        QVERIFY(!parameters.isEmpty());
        const QJsonObject period = parameters.first().toObject();
        QCOMPARE(period.value("parameter_id").toString(), QStringLiteral("mel_period_days"));
        QCOMPARE(period.value("default_value").toInt(), 3);
        QVERIFY(period.value("user_adjustable").toBool());
        QVERIFY(period.value("min").toInt() < period.value("max").toInt());
    }

    void goalHierarchyAndCycleGuard()
    {
        const auto rootUid = Domain::Uid::parse(goalUidText(kRootTitle).toStdString());
        const auto childUid = Domain::Uid::parse(goalUidText(kChildTitle).toStdString());
        QVERIFY(rootUid && childUid);

        auto goalsRepo = goals();
        Application::GoalUseCases useCases(goalsRepo, m_uids, m_clock);

        // 层级查询
        const auto children = goalsRepo.findChildren(*rootUid);
        QCOMPARE(children.size(), 1);
        QVERIFY(children.front().uid == *childUid);

        // 环检测：把根目标挂到子目标下必须被拒绝
        Domain::Goal changes;
        changes.status = Domain::GoalStatus::Active;
        changes.priority = 50;
        changes.parentGoalId = childUid;
        const auto cycleResult = useCases.reviseGoal(*rootUid, changes, 1);
        QVERIFY(!cycleResult);
        QVERIFY(cycleResult.error().code == Application::ErrorCode::Validation);

        // 父目标不存在：创建时拒绝
        const auto manifestUid = Domain::Uid::parse(manifestUidText().toStdString());
        const auto userUid = Domain::Uid::parse(kUserUid);
        QVERIFY(manifestUid && userUid);
        Application::GoalUseCases::CreateInput badParent;
        badParent.userId = *userUid;
        badParent.domainManifestId = *manifestUid;
        badParent.title = QStringLiteral("孤儿目标").toStdString();
        badParent.parentGoalId = m_uids.next(); // 随机不存在的父
        const auto orphanResult = useCases.createGoal(badParent);
        QVERIFY(!orphanResult);
        QVERIFY(orphanResult.error().code == Application::ErrorCode::Validation);
    }

    void goalRevisionConflict()
    {
        const auto rootUid = Domain::Uid::parse(goalUidText(kRootTitle).toStdString());
        QVERIFY(rootUid);

        auto goalsRepo = goals();
        Application::GoalUseCases useCases(goalsRepo, m_uids, m_clock);
        Domain::Goal changes;
        changes.status = Domain::GoalStatus::Active;
        changes.priority = 60;
        changes.title = QStringLiteral("计算机专业基础（调整）").toStdString();

        // 新库中根目标 revision=1：先用错误修订号触发冲突，再用正确值成功
        const auto stale = useCases.reviseGoal(*rootUid, changes, 999);
        QVERIFY(!stale);
        QVERIFY(stale.error().code == Application::ErrorCode::Conflict);

        const auto ok = useCases.reviseGoal(*rootUid, changes, 1);
        if (!ok)
            QFAIL(qPrintable(QString::fromStdString(ok.error().message + ": "
                                                    + ok.error().detail)));
        QCOMPARE(ok.value().revision, 2);
        QCOMPARE(ok.value().priority, 60);
    }

    void routeProposeAndConfirm()
    {
        const auto childUid = Domain::Uid::parse(goalUidText(kChildTitle).toStdString());
        QVERIFY(childUid);

        Domain::RouteStage stage;
        stage.uid = m_uids.next();
        stage.title = QStringLiteral("绪论与进程模型").toStdString();
        stage.sequenceNo = 1;
        stage.completionRuleJson = std::string("{}");
        stage.estimatedEffortMin = 120;

        auto routesRepo = routes();
        auto goalsRepo = goals();
        Application::RouteUseCases useCases(routesRepo, goalsRepo, m_uids, m_clock);

        Application::RouteUseCases::ProposeInput input;
        input.goalId = *childUid;
        input.rationale =
            QStringLiteral("先理解进程模型，再进入调度与内存管理").toStdString();
        input.evidenceSummary =
            QStringLiteral("依据：学习科学领域检索（候选）").toStdString();
        input.assumptionsJson =
            QStringLiteral("{\"prior\":\"已学 C 语言\"}").toStdString();
        input.stages = {stage};
        input.createdBy = std::string("ai"); // AI 候选
        const auto proposed = useCases.proposeRoute(input);
        if (!proposed)
            QFAIL(qPrintable(QString::fromStdString(proposed.error().message + ": "
                                                    + proposed.error().detail)));
        const auto routeUid = proposed.value().route.uid;
        QVERIFY(proposed.value().route.status == Domain::RouteStatus::Draft);
        QCOMPARE(proposed.value().versionNo, 1);

        // 版本与阶段已持久化；候选未被确认
        const auto versions = routesRepo.versionsOf(routeUid);
        QCOMPARE(versions.size(), 1);
        QVERIFY(!versions.front().userConfirmedAt.has_value());

        // 用户确认（revision=1）
        const auto confirmed = useCases.confirmRoute(routeUid, 1);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));
        QVERIFY(confirmed.value().status == Domain::RouteStatus::Confirmed);
        QVERIFY(confirmed.value().currentVersionUid.has_value());
        QVERIFY(confirmed.value().currentVersionUid.value()
                == versions.front().uid);
        QCOMPARE(confirmed.value().revision, 2);

        // 确认时间已记录；重复确认（陈旧 revision）必须冲突
        const auto versionsAfter = routesRepo.versionsOf(routeUid);
        QVERIFY(versionsAfter.front().userConfirmedAt.has_value());
        const auto stale = useCases.confirmRoute(routeUid, 1);
        QVERIFY(!stale);
        QVERIFY(stale.error().code == Application::ErrorCode::Conflict);
    }

    void legacyGoalsAreReadOnly()
    {
        QSqlQuery insert(DatabaseManager::instance().database());
        QVERIFY(insert.exec(QStringLiteral(
            "INSERT INTO goals(level,title,status,priority) VALUES('monthly','旧目标','active',50)")));
        Infrastructure::LegacyGoalReader reader(DatabaseManager::instance().database());
        bool found = false;
        for (const auto &goal : reader.readAll())
            if (goal.title == QStringLiteral("旧目标"))
                found = true;
        QVERIFY(found);
    }

private:
    QString manifestUidText()
    {
        QSqlQuery query(DatabaseManager::instance().database());
        query.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'"));
        if (!query.next())
            return {};
        return query.value(0).toString();
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

    // 仓库按需构造：必须在 DatabaseManager::open() 之后获取连接句柄，
    // 否则捕获到无效连接（qt.sql "database not open"）。
    Infrastructure::SqlGoalRepository goals()
    {
        return Infrastructure::SqlGoalRepository(DatabaseManager::instance().database(), m_clock);
    }

    Infrastructure::SqlRouteRepository routes()
    {
        return Infrastructure::SqlRouteRepository(DatabaseManager::instance().database(), m_clock);
    }

    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstGoalRoute)
#include "tst_goal_route.moc"
