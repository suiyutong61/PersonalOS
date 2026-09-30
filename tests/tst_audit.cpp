// 模块：审计事件真实写入（requirements 10.6.1；DR-026）
// 覆盖：注册 SqlAuditRepository 后，目标/复盘/MEL 转移等用例写审计事件；
//       追加式不覆盖、按对象可追溯、事件不含凭据材料；未注册 sink 时用例仍可运行。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QtConcurrent/QtConcurrent>

#include <atomic>

#include "application/audit/Audit.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/AuditDatabaseSink.h"
#include "infrastructure/persistence/SqlAuditRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kMelUid = "00000000-0000-0000-0000-0000000000bb";
} // namespace

class TstAudit : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_audit.db"));
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

        // 目标 + MEL 行（复盘用例的引用）
        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'审计MEL目标',"
            "'','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));

        QSqlQuery mel(DatabaseManager::instance().database());
        const bool melOk = mel.exec(QStringLiteral(
            "INSERT INTO mels_v4(uid,user_id,goal_id,manifest_version_id,title,state,"
            "planned_start_at,planned_end_at,timezone_id,settlement_mode,capacity_min,"
            "reserve_min,rationale,created_at,updated_at) "
            "SELECT '%1',u.id,g.id,mv.id,'审计测试','active','2026-10-01T00:00:00Z',"
            "'2026-10-04T00:00:00Z','Asia/Shanghai','deadline',300,30,'测试',"
            "'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, goals_v3 g, domain_manifest_versions_v3 mv "
            "WHERE u.uid='00000000-0000-0000-0000-0000000000aa' AND g.title='审计MEL目标' "
            "AND mv.version_no=1")
                                       .arg(QString::fromLatin1(kMelUid)));
        if (!melOk)
            QFAIL(qPrintable(QStringLiteral("mel insert: %1").arg(mel.lastError().text())));
    }

    void auditDatabaseSinkWritesFromAnyThread()
    {
        // 生产形态：线程感知 sink（每次 append 取当前线程连接）；
        // 主线程与后台 QtConcurrent 线程各写一条，均须落库
        static Infrastructure::AuditDatabaseSink sink(
            DatabaseManager::instance().databasePath());
        Application::Audit::setSink(&sink);

        QVERIFY(Application::Audit::record(
                    {"system", {}, "audit.sink_test_main", "audit", "audit-sink-test", "{}"}));
        std::atomic<bool> workerOk{false};
        QtConcurrent::run([&]() {
            const auto result = Application::Audit::record(
                {"system", {}, "audit.sink_test_worker", "audit", "audit-sink-test", "{}"});
            workerOk = result.hasValue();
        }).waitForFinished();
        QVERIFY(workerOk.load());

        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM audit_events_v6 WHERE action LIKE 'audit.sink_test_%'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 2);

        // 恢复无 sink 状态（其他用例自行注册）
        Application::Audit::setSink(nullptr);
    }

    void auditSinkRecordsBusinessChanges()
    {
        // 注册 SQL 审计 sink（组合根/测试均可注册；无 sink 时用例静默跳过）
        Infrastructure::SqlAuditRepository auditRepo(DatabaseManager::instance().database(),
                                                     m_clock, m_uids);
        Application::Audit::setSink(&auditRepo);

        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 目标创建 → goal.created
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const auto manifestUid =
            *Domain::Uid::parse(manifest.value(0).toString().toStdString());

        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = userUid;
        goalInput.domainManifestId = manifestUid;
        goalInput.title = QStringLiteral("审计追踪目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        const auto goal = goalUseCases.createGoal(goalInput);
        QVERIFY(goal);

        // 复盘开启 → review.opened
        Infrastructure::SqlReviewRepository reviewRepo(DatabaseManager::instance().database(),
                                                       m_clock);
        Application::ReviewUseCases reviewUseCases(reviewRepo, m_uids, m_clock);
        const auto melUid = *Domain::Uid::parse(kMelUid);
        const auto review = reviewUseCases.openReview(melUid);
        if (!review)
            QFAIL(qPrintable(QString::fromStdString(review.error().message + ": "
                                                    + review.error().detail)));
        QVERIFY(review.value().status == Domain::ReviewStatus::Collecting);

        // 查询审计历史：目标与复盘各至少一条，追加式
        const auto goalAudits = auditRepo.eventsOf("goal",
                                                   goal.value().goal.uid.value(), 20);
        QVERIFY(!goalAudits.empty());
        QCOMPARE(goalAudits.front().action, std::string("goal.created"));
        QVERIFY(goalAudits.front().actorType == std::string("user"));

        const auto reviewAudits = auditRepo.eventsOf("review", review.value().uid.value(), 20);
        QVERIFY(!reviewAudits.empty());
        QCOMPARE(reviewAudits.front().action, std::string("review.opened"));
        QCOMPARE(reviewAudits.front().actorType, std::string("system"));

        // 事件不含凭据材料（detail 中不得出现密钥类字样；本测试只写普通摘要）
        for (const auto &event : goalAudits)
            QVERIFY(event.detailJson.find("api_key") == std::string::npos);
    }

    void auditOffDoesNotBreakUseCases()
    {
        Application::Audit::setSink(nullptr);
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        Application::GoalUseCases::CreateInput input;
        input.userId = *Domain::Uid::parse(kUserUid);
        input.domainManifestId =
            *Domain::Uid::parse(manifest.value(0).toString().toStdString());
        input.title = QStringLiteral("无审计目标").toStdString();
        input.desiredLevelJson = std::string("{}");
        input.userDefinedLevel = true;
        QVERIFY(goalUseCases.createGoal(input));   // 无 sink 仍成功
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstAudit)
#include "tst_audit.moc"
