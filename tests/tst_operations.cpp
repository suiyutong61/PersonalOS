// IMP-007：提醒/成就/备份/启动恢复（DD-001 §12；DR-024/026/031；E2E-05）
// 覆盖：提醒规则与幂等投递、通知失败记录、成就唯一、备份快照与校验、
//       启动恢复（逾期 MEL 只结算一次）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/operations/SqliteBackup.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"

using namespace PersonOS;

namespace {
class FakeNotifications final : public Application::NotificationPort
{
public:
    bool succeed = true;
    int delivered = 0;
    bool deliver(const std::string &, const std::string &) override
    {
        ++delivered;
        return succeed;
    }
};
} // namespace

class TstOperations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_operations.db"));
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
        const bool ok = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!ok)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 目标（MEL 依赖）
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const QString manifestUid = manifest.value(0).toString();
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = *Domain::Uid::parse(kUserUid);
        goalInput.domainManifestId = *Domain::Uid::parse(manifestUid.toStdString());
        goalInput.title = QStringLiteral("运维测试目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        const auto goalResult = goalUseCases.createGoal(goalInput);
        if (!goalResult)
            QFAIL(qPrintable(QString::fromStdString(goalResult.error().message + ": "
                                                    + goalResult.error().detail)));
        m_goalUid = QString::fromStdString(goalResult.value().goal.uid.value());
        Q_UNUSED(m_goalUid)
    }

    void reminderScheduleDispatchAndIdempotency()
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        FakeNotifications notifications;
        Application::ReminderService reminders(repo, notifications, m_uids, m_clock);

        // 规则
        Application::ReminderService::RuleInput input;
        input.ownerType = "mel";
        input.ownerUid = "00000000-0000-0000-0000-0000000000dd";
        input.offsetMin = 30;
        const auto rule = reminders.createRule(input);
        if (!rule)
            QFAIL(qPrintable(QString::fromStdString(rule.error().message + ": "
                                                    + rule.error().detail)));

        // 幂等调度：同触发时刻两次只产生一批投递
        const auto first = reminders.scheduleDue("2020-01-01T00:00:00Z");
        QVERIFY(first && first.value() >= 1);
        const auto second = reminders.scheduleDue("2020-01-01T00:00:00Z");
        QVERIFY(second && second.value() == 0);

        // 投递成功
        const auto dispatched = reminders.dispatchPending();
        QVERIFY(dispatched && dispatched.value() >= 1);
        QVERIFY(notifications.delivered >= 1);

        // 通知失败只记录 failed，不改变业务状态
        FakeNotifications failing;
        failing.succeed = false;
        Application::ReminderService failingReminders(repo, failing, m_uids, m_clock);
        QVERIFY(failingReminders.scheduleDue("2020-01-02T00:00:00Z").value() >= 1);
        const auto failedDispatch = failingReminders.dispatchPending();
        QVERIFY(failedDispatch && failedDispatch.value() == 0);   // 0 成功投递

        // 关闭提醒
        const auto disabled = reminders.disableRule(rule.value().uid, 1);
        QVERIFY(disabled && !disabled.value().enabled);
        // 关闭后不产生投递
        QVERIFY(reminders.scheduleDue("2020-01-03T00:00:00Z").value() == 0);
    }

    void achievementUniqueness()
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::AchievementService achievements(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        const auto earned = achievements.earn(
            userUid, "mel_completed", QStringLiteral("完成第一个 MEL").toStdString(),
            QStringLiteral("真实执行完成事件").toStdString(), "mel",
            "00000000-0000-0000-0000-0000000000ee", std::string("{}"));
        if (!earned)
            QFAIL(qPrintable(QString::fromStdString(earned.error().message + ": "
                                                    + earned.error().detail)));
        // 唯一约束：同源重复解锁拒绝
        const auto duplicate = achievements.earn(
            userUid, "mel_completed", QStringLiteral("完成第一个 MEL").toStdString(),
            QStringLiteral("重复").toStdString(), "mel", "00000000-0000-0000-0000-0000000000ee");
        QVERIFY(!duplicate);
        QVERIFY(duplicate.error().code == Application::ErrorCode::Conflict);
    }

    void backupSnapshotAndVerify()
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqliteBackup snapshots;
        Application::BackupService backups(repo, snapshots, m_uids, m_clock);

        const std::string target =
            (QDir::temp().filePath(QStringLiteral("personos_backup_snapshot.db"))).toStdString();
        QFile::remove(QString::fromStdString(target));

        Application::BackupService::CreateInput input;
        input.sourceDbPath = m_path.toStdString();
        input.targetPath = target;
        input.dbSchemaVersion = 6;
        const auto backup = backups.createBackup(input);
        if (!backup)
            QFAIL(qPrintable(QString::fromStdString(backup.error().message + ": "
                                                    + backup.error().detail)));
        QVERIFY(backup.value().status == "verified");
        QVERIFY(backup.value().sha256.has_value());
        QVERIFY(QFile::exists(QString::fromStdString(target)));

        // 校验通过
        const auto verified = backups.verifyBackup(backup.value().uid);
        QVERIFY(verified && verified.value());

        // 篡改后校验失败
        QFile tampered(QString::fromStdString(target));
        QVERIFY(tampered.open(QIODevice::Append));
        tampered.write("tamper");
        tampered.close();
        const auto verifyAfterTamper = backups.verifyBackup(backup.value().uid);
        QVERIFY(verifyAfterTamper && !verifyAfterTamper.value());
        QFile::remove(QString::fromStdString(target));
    }

    void startupRecoverySettlesDueMelsOnce()
    {
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::MelUseCases melUseCases(melRepo, m_uids, m_clock);
        Infrastructure::SqlOperationsRepository opsRepo(
            DatabaseManager::instance().database(), m_clock);
        FakeNotifications notifications;
        Application::ReminderService reminders(opsRepo, notifications, m_uids, m_clock);
        Application::StartupRecovery recovery(melRepo, melUseCases, reminders, m_clock);

        // 过期窗口 MEL：创建→确认→激活
        Application::MelUseCases::CreateInput input;
        input.userId = *Domain::Uid::parse(kUserUid);
        input.goalId = *Domain::Uid::parse(
            [&] {
                QSqlQuery q(DatabaseManager::instance().database());
                q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='运维测试目标'"));
                q.next();
                return q.value(0).toString();
            }()
                .toStdString());
        input.manifestVersionId = *Domain::Uid::parse(
            [&] {
                QSqlQuery q(DatabaseManager::instance().database());
                q.exec(QStringLiteral(
                    "SELECT uid FROM domain_manifest_versions_v3 WHERE version_no=1"));
                q.next();
                return q.value(0).toString();
            }()
                .toStdString());
        input.title = QStringLiteral("历史逾期 MEL").toStdString();
        input.plannedStartAt = "2020-01-01T00:00:00Z";
        input.plannedEndAt = "2020-01-04T00:00:00Z";
        input.capacityMin = 120;
        input.reserveMin = 10;
        input.rationale = "启动恢复测试";
        Domain::MelTask task;
        task.title = QStringLiteral("旧任务").toStdString();
        task.sequenceNo = 0;
        task.plannedEffortMin = 30;
        input.tasks = {task};
        const auto created = melUseCases.createMelProposal(input);
        QVERIFY(created);
        QVERIFY(melUseCases.submitForConfirmation(created.value().mel.uid, 1));
        QVERIFY(melUseCases.confirmAndActivate(created.value().mel.uid, 2));

        // 第一次恢复：找到逾期 MEL 并结算
        const auto first = recovery.run();
        if (!first)
            QFAIL(qPrintable(QString::fromStdString(first.error().message + ": "
                                                    + first.error().detail)));
        QVERIFY(first.value().dueMelsFound >= 1);
        QVERIFY(first.value().settled >= 1);

        // 第二次恢复：settle 幂等，不再产生新的结算（E2E-05）
        const auto second = recovery.run();
        QVERIFY(second);
        QCOMPARE(second.value().settled, 0);
    }

private:
    QString m_path;
    QString m_goalUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    static constexpr const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
};

QTEST_GUILESS_MAIN(TstOperations)
#include "tst_operations.moc"
