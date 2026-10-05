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
    std::string lastTitle;
    std::string lastBody;
    bool deliver(const std::string &title, const std::string &body) override
    {
        ++delivered;
        lastTitle = title;
        lastBody = body;
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
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        FakeNotifications notifications;
        Application::ReminderService reminders(repo, melRepo, notifications, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 活跃 MEL（Deadline 在过去）
        createOverdueMel(melRepo, QStringLiteral("提醒测试 MEL"),
                         QStringLiteral("2020-01-04T00:00:00Z").toStdString());

        // 自动默认规则：幂等（第二次不再新建）
        const auto ensured = reminders.ensureMelDeadlineReminders(userUid, 50);
        QVERIFY(ensured && ensured.value() >= 1);
        const auto ensuredAgain = reminders.ensureMelDeadlineReminders(userUid, 50);
        QVERIFY(ensuredAgain && ensuredAgain.value() == 0);

        // 到期计算 = Deadline − 0 分钟（2020 年已过）→ 生成投递；幂等键
        // = rule uid + 到期时刻，同一次到期只产生一批（DB-06）
        const auto first = reminders.scheduleDue("2020-02-01T00:00:00Z");
        QVERIFY(first && first.value() >= 1);
        const auto second = reminders.scheduleDue("2020-02-01T00:00:00Z");
        QVERIFY(second && second.value() == 0);

        // 投递成功：通知内容包含 MEL 标题与 Deadline 语义
        const auto dispatched = reminders.dispatchPending();
        QVERIFY(dispatched && dispatched.value() >= 1);
        QVERIFY(notifications.delivered >= 1);
        QVERIFY2(QString::fromStdString(notifications.lastBody)
                     .contains(QStringLiteral("提醒测试 MEL")),
                 "notification body must contain the MEL title");
        QVERIFY2(QString::fromStdString(notifications.lastBody)
                     .contains(QStringLiteral("已到 Deadline")),
                 "zero-offset reminder must mention the deadline");

        // 通知失败：failed 计入重试；三次尝试后终态失败不再重选
        FakeNotifications failing;
        failing.succeed = false;
        Application::ReminderService failingReminders(repo, melRepo, failing, m_uids, m_clock);
        createOverdueMel(melRepo, QStringLiteral("提醒测试 MEL 二"),
                         QStringLiteral("2020-01-05T00:00:00Z").toStdString());
        QCOMPARE(failingReminders.ensureMelDeadlineReminders(userUid, 50).value(), 1);
        QCOMPARE(failingReminders.scheduleDue("2020-02-01T00:00:00Z").value(), 1);
        for (int attempt = 0; attempt < 4; ++attempt)
            QCOMPARE(failingReminders.dispatchPending().value(), 0);   // 三次尝试后无待投递
        QSqlQuery failedRow(DatabaseManager::instance().database());
        QVERIFY(failedRow.exec(QStringLiteral(
            "SELECT status, attempt_count FROM reminder_deliveries_v6 "
            "ORDER BY id DESC LIMIT 1")));
        QVERIFY(failedRow.next());
        QCOMPARE(failedRow.value(0).toString(), QStringLiteral("failed"));
        QCOMPARE(failedRow.value(1).toInt(), 3);

        // 静默时段：全天静默 → suppressed（不打扰用户，也不伪装成功）
        createOverdueMel(melRepo, QStringLiteral("提醒测试 MEL 三"),
                         QStringLiteral("2020-01-06T00:00:00Z").toStdString());
        QCOMPARE(reminders.ensureMelDeadlineReminders(userUid, 50).value(), 1);
        QSqlQuery quietRuleQ(DatabaseManager::instance().database());
        quietRuleQ.prepare(QStringLiteral(
            "SELECT uid FROM reminder_rules_v6 WHERE owner_uid="
            "(SELECT uid FROM mels_v4 WHERE title=?)"));
        quietRuleQ.addBindValue(QStringLiteral("提醒测试 MEL 三"));
        QVERIFY(quietRuleQ.exec() && quietRuleQ.next());
        const auto quietRuleUid =
            *Domain::Uid::parse(quietRuleQ.value(0).toString().toStdString());
        const auto quietRule = repo.findRule(quietRuleUid);
        QVERIFY(quietRule);
        auto updatedQuiet = *quietRule;
        updatedQuiet.quietHoursJson = std::string("{\"start\":\"00:00\",\"end\":\"23:59\"}");
        QVERIFY(repo.updateRule(updatedQuiet, quietRule->revision).ok);
        QVERIFY(reminders.scheduleDue("2020-02-01T00:00:00Z").value() == 1);
        QCOMPARE(reminders.dispatchPending().value(), 0);   // 静默不投递
        QSqlQuery suppressedCount(DatabaseManager::instance().database());
        QVERIFY(suppressedCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM reminder_deliveries_v6 WHERE status='suppressed'")));
        QVERIFY(suppressedCount.next());
        QCOMPARE(suppressedCount.value(0).toInt(), 1);

        // 提前量：Deadline 在 1 小时后、提前 30 分钟 → 现在不调度
        const auto futureMel = createActiveMel(melRepo, QStringLiteral("提醒测试 MEL 四"),
                                               m_clock.utcIso(),
                                               m_clock.utcIsoPlusMinutes(60));
        QVERIFY(!futureMel.empty());
        Application::ReminderService::RuleInput futureInput;
        futureInput.ownerType = "mel";
        futureInput.ownerUid = futureMel.value();
        futureInput.offsetMin = 30;
        QVERIFY(reminders.createRule(futureInput));
        QCOMPARE(reminders.scheduleDue(m_clock.utcIso()).value(), 0);   // dueAt 在未来

        // 停用规则后不再调度；ensure 不自动重建（停用是用户决定）
        const auto disabled =
            reminders.disableRule(quietRuleUid, updatedQuiet.revision + 1);
        QVERIFY(disabled && !disabled.value().enabled);
        QVERIFY(reminders.ensureMelDeadlineReminders(userUid, 50).value() == 0);

        // 重新启用（规则管理 UI 开关）
        const auto reenabled =
            reminders.enableRule(quietRuleUid, updatedQuiet.revision + 2);
        QVERIFY(reenabled && reenabled.value().enabled);
    }

    void reminderUsesAbsoluteTimeAndRechecksEligibility()
    {
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        FakeNotifications notifications;
        Application::ReminderService reminders(repo, melRepo, notifications, m_uids, m_clock);

        // 同一绝对时间使用 +08:00 表达。旧代码按 ISO 字符串比较，08:00+08:00
        // 会被错误判断为晚于 00:30Z，从而永不调度。
        const auto offsetMel = createActiveMel(
            melRepo, QStringLiteral("时区偏移提醒 MEL"),
            "2020-01-01T08:00:00+08:00", "2020-01-04T08:00:00+08:00");
        QVERIFY(!offsetMel.empty());
        Application::ReminderService::RuleInput offsetInput;
        offsetInput.ownerType = "mel";
        offsetInput.ownerUid = offsetMel.value();
        offsetInput.offsetMin = 0;
        const auto offsetRule = reminders.createRule(offsetInput);
        QVERIFY(offsetRule);
        QVERIFY(reminders.scheduleDue("2020-01-04T00:30:00Z").value() >= 1);
        QSqlQuery offsetDelivery(DatabaseManager::instance().database());
        offsetDelivery.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM reminder_deliveries_v6 WHERE rule_id="
            "(SELECT id FROM reminder_rules_v6 WHERE uid=?)"));
        offsetDelivery.addBindValue(
            QString::fromStdString(offsetRule.value().uid.value()));
        QVERIFY(offsetDelivery.exec() && offsetDelivery.next());
        QCOMPARE(offsetDelivery.value(0).toInt(), 1);
        QCOMPARE(reminders.dispatchPending().value(), 1);
        QCOMPARE(notifications.delivered, 1);
    }

    void reminderRechecksEligibilityBeforeDispatch()
    {
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        FakeNotifications notifications;
        Application::ReminderService reminders(repo, melRepo, notifications, m_uids, m_clock);

        // 已排队后停用规则：失败重试/待投递必须重新尊重用户决定。
        const auto disabledMel = createOverdueMel(
            melRepo, QStringLiteral("停用后不投递 MEL"), "2020-01-08T00:00:00Z");
        Application::ReminderService::RuleInput disabledInput;
        disabledInput.ownerType = "mel";
        disabledInput.ownerUid = disabledMel.value();
        const auto disabledRule = reminders.createRule(disabledInput);
        QVERIFY(disabledRule);
        QVERIFY(reminders.scheduleDue("2020-02-01T00:00:00Z").value() >= 1);
        QVERIFY(reminders.disableRule(disabledRule.value().uid,
                                      disabledRule.value().revision));

        // 已排队后 MEL 执行完成：同样不得再弹 Deadline 提醒。
        const auto completedMel = createOverdueMel(
            melRepo, QStringLiteral("完成后不投递 MEL"), "2020-01-09T00:00:00Z");
        Application::ReminderService::RuleInput completedInput;
        completedInput.ownerType = "mel";
        completedInput.ownerUid = completedMel.value();
        const auto completedRule = reminders.createRule(completedInput);
        QVERIFY(completedRule);
        QVERIFY(reminders.scheduleDue("2020-02-01T00:00:00Z").value() >= 1);
        QSqlQuery complete(DatabaseManager::instance().database());
        complete.prepare(QStringLiteral(
            "UPDATE mels_v4 SET state='execution_complete' WHERE uid=?"));
        complete.addBindValue(QString::fromStdString(completedMel.value()));
        QVERIFY(complete.exec());

        notifications.delivered = 0;
        QVERIFY(reminders.dispatchPending());
        QCOMPARE(notifications.delivered, 0);
        QSqlQuery suppressed(DatabaseManager::instance().database());
        suppressed.prepare(QStringLiteral(
            "SELECT status FROM reminder_deliveries_v6 WHERE rule_id="
            "(SELECT id FROM reminder_rules_v6 WHERE uid=?)"));
        suppressed.bindValue(0,
            QString::fromStdString(disabledRule.value().uid.value()));
        QVERIFY(suppressed.exec() && suppressed.next());
        QCOMPARE(suppressed.value(0).toString(), QStringLiteral("suppressed"));
        suppressed.finish();
        suppressed.bindValue(0,
            QString::fromStdString(completedRule.value().uid.value()));
        QVERIFY(suppressed.exec() && suppressed.next());
        QCOMPARE(suppressed.value(0).toString(), QStringLiteral("suppressed"));
    }

    void deliveryRetryAndOwnerRulesAtRepositoryLevel()
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        // 规则（owner 独立于既有槽位，避免互扰）
        Domain::ReminderRule rule;
        rule.uid = m_uids.next();
        rule.ownerType = "mel";
        rule.ownerUid = "00000000-0000-0000-0000-0000000000de";
        rule.offsetMin = 0;
        rule.channel = "app";
        QVERIFY(repo.insertRule(rule).ok);

        // rulesForOwner 含停用规则（停用是用户决定，不得自动重建默认规则）
        QCOMPARE(repo.rulesForOwner("mel", "00000000-0000-0000-0000-0000000000de").size(), 1);
        auto disabled = rule;
        disabled.enabled = false;
        QVERIFY(repo.updateRule(disabled, 1).ok);
        QCOMPARE(repo.rulesForOwner("mel", "00000000-0000-0000-0000-0000000000de").size(), 1);

        // allRules 含停用规则（规则管理 UI 需要展示全部规则）
        bool sawMine = false;
        for (const auto &allRule : repo.allRules())
            if (allRule.uid == rule.uid)
                sawMine = true;
        QVERIFY(sawMine);

        // 投递行：failed 重选直到 attempt_count=3，之后终态失败不再重选
        Domain::ReminderDelivery delivery;
        delivery.uid = m_uids.next();
        delivery.ruleUid = rule.uid;
        delivery.scheduledAt = "2020-01-01T00:00:00Z";
        delivery.idempotencyKey = "reminder:repo-test:retry:1";
        QVERIFY(repo.insertDelivery(delivery).ok);

        // 注：既有槽位留下的 failed 行（attempt<3）也会被重选，断言按幂等键
        // 定位本槽位的行，不断言全表数量
        const auto findMine = [](const std::vector<Domain::ReminderDelivery> &rows,
                                 const std::string &key)
            -> const Domain::ReminderDelivery * {
            for (const auto &row : rows)
                if (row.idempotencyKey == key)
                    return &row;
            return nullptr;
        };

        auto due = repo.pendingDeliveries("2020-02-01T00:00:00Z");
        QVERIFY(findMine(due, "reminder:repo-test:retry:1") != nullptr);
        QCOMPARE(findMine(due, "reminder:repo-test:retry:1")->attemptCount, 0);
        QCOMPARE(findMine(due, "reminder:repo-test:retry:1")->ruleUid, rule.uid);

        for (int attempt = 1; attempt <= 3; ++attempt) {
            QVERIFY(repo.markDelivery(delivery.uid, "failed", "notification failed").ok);
            due = repo.pendingDeliveries("2020-02-01T00:00:00Z");
            const auto mine = findMine(due, "reminder:repo-test:retry:1");
            if (attempt < 3)
                QVERIFY(mine != nullptr);   // 未达上限继续重选
            else
                QVERIFY(mine == nullptr);   // 终态失败不再重选
        }

        // 投递成功：delivered_at 落库、attempt_count=1、recentDeliveries 最新在前
        Domain::ReminderDelivery good;
        good.uid = m_uids.next();
        good.ruleUid = rule.uid;
        good.scheduledAt = "2020-01-02T00:00:00Z";
        good.idempotencyKey = "reminder:repo-test:retry:2";
        QVERIFY(repo.insertDelivery(good).ok);
        QVERIFY(repo.markDelivery(good.uid, "delivered", std::nullopt).ok);

        const auto recent = repo.recentDeliveries(10);
        QVERIFY(recent.size() >= 2);
        QVERIFY(recent.front().uid == good.uid);   // 最新在前
        QCOMPARE(recent.front().status, std::string("delivered"));
        QVERIFY(recent.front().deliveredAt.has_value());
        QCOMPARE(recent.front().attemptCount, 1);
        const auto failedRow = recent[1];
        QCOMPARE(failedRow.status, std::string("failed"));
        QCOMPARE(failedRow.attemptCount, 3);
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
        Application::ReminderService reminders(opsRepo, melRepo, notifications, m_uids,
                                               m_clock);
        Application::StartupRecovery recovery(melRepo, melUseCases, reminders,
                                              *Domain::Uid::parse(kUserUid), m_clock);

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

        // findByUser：含非活跃 MEL（候选/历史），最新在前（首个 MEL 候选展示）
        const auto allMels =
            melRepo.findByUser(*Domain::Uid::parse(kUserUid), 10);
        QVERIFY(!allMels.empty());
        QVERIFY(allMels.front().uid == created.value().mel.uid);

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
    QString goalUidText()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='运维测试目标'"));
        if (!q.next())
            return {};
        return q.value(0).toString();
    }

    QString manifestVersionUidText()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM domain_manifest_versions_v3 WHERE version_no=1"));
        if (!q.next())
            return {};
        return q.value(0).toString();
    }

    // 创建并激活 MEL（Deadline 在过去 → 逾期未结算，供调度语义测试）
    Domain::Uid createOverdueMel(Infrastructure::SqlMelRepository &melRepo,
                                 const QString &title, const std::string &plannedEndAt)
    {
        return createActiveMel(melRepo, title, "2020-01-01T00:00:00Z", plannedEndAt);
    }

    Domain::Uid createActiveMel(Infrastructure::SqlMelRepository &melRepo,
                                const QString &title, const std::string &plannedStartAt,
                                const std::string &plannedEndAt)
    {
        Application::MelUseCases melUseCases(melRepo, m_uids, m_clock);
        Application::MelUseCases::CreateInput input;
        input.userId = *Domain::Uid::parse(kUserUid);
        input.goalId = *Domain::Uid::parse(goalUidText().toStdString());
        input.manifestVersionId =
            *Domain::Uid::parse(manifestVersionUidText().toStdString());
        input.title = title.toStdString();
        input.plannedStartAt = plannedStartAt;
        input.plannedEndAt = plannedEndAt;
        input.capacityMin = 120;
        input.reserveMin = 10;
        input.rationale = "提醒测试";
        Domain::MelTask task;
        task.title = QStringLiteral("任务").toStdString();
        task.sequenceNo = 0;
        task.plannedEffortMin = 30;
        input.tasks = {task};
        const auto created = melUseCases.createMelProposal(input);
        if (!created)
            return {};
        if (!melUseCases.submitForConfirmation(created.value().mel.uid, 1))
            return {};
        if (!melUseCases.confirmAndActivate(created.value().mel.uid, 2))
            return {};
        return created.value().mel.uid;
    }

    QString m_path;
    QString m_goalUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    static constexpr const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
};

QTEST_GUILESS_MAIN(TstOperations)
#include "tst_operations.moc"
