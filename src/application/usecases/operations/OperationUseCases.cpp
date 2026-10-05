#include "application/usecases/operations/OperationUseCases.h"

#include "application/audit/Audit.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTime>
#include <QUuid>

namespace PersonOS::Application {

// ---------------------------------------------------------------- Reminder

namespace {

// UTC ISO-8601 减 N 分钟（解析失败返回 nullopt，调用方跳过该规则不猜测）
std::optional<std::string> isoMinusMinutes(const std::string &isoUtc, int minutes)
{
    const QDateTime parsed = QDateTime::fromString(QString::fromStdString(isoUtc),
                                                   Qt::ISODate);
    if (!parsed.isValid())
        return std::nullopt;
    return parsed.addSecs(-minutes * 60).toString(Qt::ISODate).toStdString();
}

// ISO-8601 不能直接按字符串比较：同一时刻可以写成 Z、+08:00 或带毫秒。
// 统一解析为绝对时间后再判断，避免非 UTC 偏移格式的提醒永远不触发。
bool isoAtOrBefore(const std::string &value, const std::string &reference)
{
    const QDateTime parsedValue = QDateTime::fromString(
        QString::fromStdString(value), Qt::ISODate);
    const QDateTime parsedReference = QDateTime::fromString(
        QString::fromStdString(reference), Qt::ISODate);
    return parsedValue.isValid() && parsedReference.isValid()
           && parsedValue.toUTC() <= parsedReference.toUTC();
}

// 静默时段判定（{"start":"HH:mm","end":"HH:mm"}，本地时区；start>end 表示跨午夜）
bool inQuietHours(const std::string &quietHoursJson, const QTime &localNow)
{
    const QJsonObject object =
        QJsonDocument::fromJson(QByteArray::fromStdString(quietHoursJson)).object();
    const QTime start = QTime::fromString(object.value(QStringLiteral("start")).toString(),
                                          QStringLiteral("HH:mm"));
    const QTime end = QTime::fromString(object.value(QStringLiteral("end")).toString(),
                                        QStringLiteral("HH:mm"));
    if (!start.isValid() || !end.isValid() || start == end)
        return false;
    if (start < end)
        return localNow >= start && localNow < end;
    return localNow >= start || localNow < end;   // 跨午夜窗口
}

// 提醒对象仅限"开放中"的 MEL：未确认/草稿/已关闭/已取消/已执行完成的
// MEL 不投递（提前完成的 MEL 不应在 Deadline 时打扰用户）
bool melOpenForReminder(const Domain::Mel &mel)
{
    if (!mel.confirmedAt)
        return false;
    switch (mel.state) {
    case Domain::MelState::Draft:
    case Domain::MelState::AwaitingConfirmation:
    case Domain::MelState::ExecutionComplete:
    case Domain::MelState::Closed:
    case Domain::MelState::Cancelled:
        return false;
    default:
        return true;
    }
}

} // namespace

ReminderService::ReminderService(ReminderRepositoryPort &repo, MelRepository &mels,
                                 NotificationPort &notifications, UuidPort &uids,
                                 const Domain::Clock &clock)
    : m_repo(repo), m_mels(mels), m_notifications(notifications), m_uids(uids), m_clock(clock)
{}

Result<Domain::ReminderRule, ApplicationError> ReminderService::createRule(
    const RuleInput &input)
{
    Domain::ReminderRule rule;
    rule.uid = m_uids.next();
    rule.eventUid = input.eventUid;
    rule.ownerType = input.ownerType;
    rule.ownerUid = input.ownerUid;
    rule.offsetMin = input.offsetMin;
    rule.channel = input.channel;
    const auto saved = m_repo.insertRule(rule);
    if (!saved.ok)
        return Result<Domain::ReminderRule, ApplicationError>::failure(saved.error);
    return Result<Domain::ReminderRule, ApplicationError>::success(std::move(rule));
}

Result<Domain::ReminderRule, ApplicationError> ReminderService::disableRule(
    const Domain::Uid &ruleUid, int expectedRevision)
{
    const auto current = m_repo.findRule(ruleUid);
    if (!current)
        return Result<Domain::ReminderRule, ApplicationError>::failure(
            {ErrorCode::NotFound, "reminder rule not found", {}, false});
    Domain::ReminderRule updated = *current;
    updated.enabled = false;
    const auto saved = m_repo.updateRule(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ReminderRule, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::ReminderRule, ApplicationError>::success(std::move(updated));
}

Result<Domain::ReminderRule, ApplicationError> ReminderService::enableRule(
    const Domain::Uid &ruleUid, int expectedRevision)
{
    const auto current = m_repo.findRule(ruleUid);
    if (!current)
        return Result<Domain::ReminderRule, ApplicationError>::failure(
            {ErrorCode::NotFound, "reminder rule not found", {}, false});
    Domain::ReminderRule updated = *current;
    updated.enabled = true;
    const auto saved = m_repo.updateRule(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ReminderRule, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::ReminderRule, ApplicationError>::success(std::move(updated));
}

Result<int, ApplicationError> ReminderService::ensureMelDeadlineReminders(
    const Domain::Uid &userId, int limit)
{
    int created = 0;
    for (const auto &mel : m_mels.findActive(userId, limit)) {
        // 已有规则（含停用）视为用户已决定，不自动重建
        if (!m_repo.rulesForOwner("mel", mel.uid.value()).empty())
            continue;
        RuleInput input;
        input.ownerType = "mel";
        input.ownerUid = mel.uid.value();
        input.offsetMin = 0;
        input.channel = "app";
        const auto rule = createRule(input);
        if (!rule)
            return Result<int, ApplicationError>::failure(rule.error());
        ++created;
    }
    return Result<int, ApplicationError>::success(created);
}

Result<int, ApplicationError> ReminderService::scheduleDue(const std::string &triggerAtIso)
{
    int created = 0;
    for (const auto &rule : m_repo.enabledRules()) {
        // v1 只调度绑定 MEL 的规则；其余类型如实跳过（不猜测到期时刻）
        if (rule.ownerType != "mel")
            continue;
        const auto melUid = Domain::Uid::parse(rule.ownerUid);
        if (!melUid)
            continue;
        const auto mel = m_mels.findByUid(*melUid);
        if (!mel || !melOpenForReminder(*mel))
            continue;
        // 到期时刻 = Deadline − 提前量（解析失败跳过，不猜测）
        const auto dueAt = isoMinusMinutes(mel->plannedEndAt, rule.offsetMin);
        if (!dueAt)
            continue;
        if (!isoAtOrBefore(*dueAt, triggerAtIso))
            continue;   // 未到期
        // 幂等键 = rule uid + 到期时刻：同一次到期只产生一批投递，
        // 重启补发与新到期自然区分（DB-06）
        const std::string key = "reminder:" + rule.uid.value() + ":" + *dueAt;
        if (m_repo.existsDeliveryKey(key))
            continue;
        Domain::ReminderDelivery delivery;
        delivery.uid = m_uids.next();
        delivery.ruleUid = rule.uid;
        delivery.scheduledAt = *dueAt;
        delivery.idempotencyKey = key;
        const auto saved = m_repo.insertDelivery(delivery);
        if (!saved.ok)
            return Result<int, ApplicationError>::failure(saved.error);
        ++created;
    }
    return Result<int, ApplicationError>::success(created);
}

Result<int, ApplicationError> ReminderService::dispatchPending()
{
    int delivered = 0;
    const std::string nowIso = m_clock.utcIso();
    for (const auto &pending : m_repo.pendingDeliveries(nowIso)) {
        const auto rule = m_repo.findRule(pending.ruleUid);
        // 已排队的失败重试也必须重新尊重用户当前决定。否则用户停用规则，
        // 或 MEL 已执行完成后，旧 pending/failed 行仍会继续弹出提醒。
        if (!rule || !rule->enabled) {
            const auto suppressed = m_repo.markDelivery(
                pending.uid, "suppressed", std::string("rule disabled or missing"));
            if (!suppressed.ok)
                return Result<int, ApplicationError>::failure(suppressed.error);
            continue;
        }
        if (rule->ownerType == "mel") {
            const auto melUid = Domain::Uid::parse(rule->ownerUid);
            const auto mel = melUid ? m_mels.findByUid(*melUid) : std::nullopt;
            if (!mel || !melOpenForReminder(*mel)) {
                const auto suppressed = m_repo.markDelivery(
                    pending.uid, "suppressed", std::string("mel no longer open"));
                if (!suppressed.ok)
                    return Result<int, ApplicationError>::failure(suppressed.error);
                continue;
            }
        }
        // 静默时段：如实记 suppressed（不打扰用户，也不伪装成功）
        if (inQuietHours(rule->quietHoursJson, QDateTime::currentDateTime().time())) {
            const auto suppressed = m_repo.markDelivery(
                pending.uid, "suppressed", std::string("quiet hours"));
            if (!suppressed.ok)
                return Result<int, ApplicationError>::failure(suppressed.error);
            continue;
        }

        // 通知内容：绑定 MEL 时给出具体事项（不绑定则保持通用文案）
        std::string title = "Personal OS 提醒";
        std::string body = "您有到期的提醒事项";
        if (rule->ownerType == "mel") {
            if (const auto melUid = Domain::Uid::parse(rule->ownerUid))
                if (const auto mel = m_mels.findByUid(*melUid)) {
                    title = "Personal OS 提醒";
                    body = "「" + mel->title + "」"
                           + (rule->offsetMin > 0
                                  ? " 距 Deadline 还有 " + std::to_string(rule->offsetMin)
                                        + " 分钟"
                                  : " 已到 Deadline");
                }
        }

        const bool ok = m_notifications.deliver(title, body);
        const auto marked = m_repo.markDelivery(
            pending.uid, ok ? "delivered" : "failed",
            ok ? std::optional<std::string>() : std::string("notification failed"));
        if (!marked.ok)
            return Result<int, ApplicationError>::failure(marked.error);
        if (ok)
            ++delivered;
    }
    return Result<int, ApplicationError>::success(delivered);
}

// ---------------------------------------------------------------- Achievement

AchievementService::AchievementService(AchievementRepositoryPort &repo, UuidPort &uids,
                                       const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<Domain::Achievement, ApplicationError> AchievementService::earn(
    const Domain::Uid &userId, const std::string &type, const std::string &title,
    const std::string &description, const std::string &sourceType,
    const std::string &sourceUid, const std::string &evidenceJson)
{
    if (m_repo.exists(userId, type, sourceType, sourceUid))
        return Result<Domain::Achievement, ApplicationError>::failure(
            {ErrorCode::Conflict, "achievement already earned", {}, false});

    Domain::Achievement achievement;
    achievement.uid = m_uids.next();
    achievement.userId = userId;
    achievement.achievementType = type;
    achievement.title = title;
    achievement.description = description;
    achievement.earnedAt = m_clock.utcIso();
    achievement.sourceType = sourceType;
    achievement.sourceUid = sourceUid;
    achievement.evidenceJson = evidenceJson;
    const auto saved = m_repo.insert(achievement);
    if (!saved.ok)
        return Result<Domain::Achievement, ApplicationError>::failure(saved.error);
    return Result<Domain::Achievement, ApplicationError>::success(std::move(achievement));
}

// ---------------------------------------------------------------- Backup

BackupService::BackupService(BackupRepositoryPort &repo, BackupPort &snapshots,
                             UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_snapshots(snapshots), m_uids(uids), m_clock(clock)
{}

Result<Domain::BackupRecord, ApplicationError> BackupService::createBackup(
    const CreateInput &input)
{
    if (!QFile::exists(QString::fromStdString(input.sourceDbPath)))
        return Result<Domain::BackupRecord, ApplicationError>::failure(
            {ErrorCode::NotFound, "source database not found", {}, false});

    Domain::BackupRecord record;
    record.uid = m_uids.next();
    record.startedAt = m_clock.utcIso();
    record.relativePath = input.targetPath;
    record.dbSchemaVersion = input.dbSchemaVersion;
    const auto registered = m_repo.insert(record);
    if (!registered.ok)
        return Result<Domain::BackupRecord, ApplicationError>::failure(registered.error);

    // 一致性快照（基础设施实现：SQLite VACUUM INTO）
    const auto snapshot = m_snapshots.createSnapshot(input.sourceDbPath, input.targetPath);
    if (!snapshot) {
        record.status = "failed";
        record.errorJson = "{\"error\":\"" + snapshot.error().message + "\"}";
        m_repo.update(record);
        return Result<Domain::BackupRecord, ApplicationError>::failure(snapshot.error());
    }

    // SHA-256 校验
    QFile backup(QString::fromStdString(input.targetPath));
    if (!backup.open(QIODevice::ReadOnly)) {
        record.status = "failed";
        record.errorJson = "{\"error\":\"backup file unreadable\"}";
        m_repo.update(record);
        return Result<Domain::BackupRecord, ApplicationError>::failure(
            {ErrorCode::Storage, "backup file unreadable", {}, false});
    }
    const QByteArray data = backup.readAll();
    backup.close();
    const std::string sha =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();

    record.sha256 = sha;
    record.status = "verified";
    record.completedAt = m_clock.utcIso();
    const auto saved = m_repo.update(record);
    if (!saved.ok)
        return Result<Domain::BackupRecord, ApplicationError>::failure(saved.error);
    return Result<Domain::BackupRecord, ApplicationError>::success(std::move(record));
}

Result<bool, ApplicationError> BackupService::verifyBackup(const Domain::Uid &backupUid)
{
    const auto record = m_repo.find(backupUid);
    if (!record)
        return Result<bool, ApplicationError>::failure(
            {ErrorCode::NotFound, "backup record not found", {}, false});
    if (!record->sha256)
        return Result<bool, ApplicationError>::failure(
            {ErrorCode::Storage, "backup has no checksum", {}, false});
    QFile file(QString::fromStdString(record->relativePath));
    if (!file.open(QIODevice::ReadOnly))
        return Result<bool, ApplicationError>::failure(
            {ErrorCode::Storage, "backup file missing", {}, false});
    const QByteArray data = file.readAll();
    file.close();
    const std::string sha =
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
    return Result<bool, ApplicationError>::success(sha == *record->sha256);
}

// ---------------------------------------------------------------- StartupRecovery

RestoreService::RestoreService(BackupRepositoryPort &repo, DatabaseSwitchPort &switcher,
                               UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_switcher(switcher), m_uids(uids), m_clock(clock)
{}

Result<RestoreService::RestoreReport, ApplicationError> RestoreService::restore(
    const Domain::Uid &backupUid, int supportedSchemaVersion)
{
    RestoreReport report;

    // 1) 备份记录与状态（只允许恢复已校验的备份）
    const auto record = m_repo.find(backupUid);
    if (!record)
        return Result<RestoreReport, ApplicationError>::failure(
            {ErrorCode::NotFound, "backup record not found", {}, false});
    if (record->status != "verified")
        return Result<RestoreReport, ApplicationError>::failure(
            {ErrorCode::Conflict, "backup is not verified", {}, false});

    // 2) 文件存在性 + SHA-256 完整性校验（篡改即拒绝）
    const QString backupPath = QString::fromStdString(record->relativePath);
    if (!QFile::exists(backupPath))
        return Result<RestoreReport, ApplicationError>::failure(
            {ErrorCode::NotFound, "backup file missing", {}, false});
    if (record->sha256) {
        QFile file(backupPath);
        if (!file.open(QIODevice::ReadOnly))
            return Result<RestoreReport, ApplicationError>::failure(
                {ErrorCode::Storage, "backup file unreadable", {}, false});
        const QByteArray data = file.readAll();
        file.close();
        const std::string actual =
            QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toStdString();
        if (actual != *record->sha256)
            return Result<RestoreReport, ApplicationError>::failure(
                {ErrorCode::Conflict, "backup hash mismatch (tampered)", {}, false});
    }

    // 3) 临时区完整性检查（PRAGMA integrity_check + schema 版本兼容：
    //    备份 schema 不得高于当前程序支持的版本）
    {
        const QString tempName = QStringLiteral("restore_check_%1")
                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        {
            QSqlDatabase check = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), tempName);
            check.setDatabaseName(backupPath);
            if (!check.open()) {
                QSqlDatabase::removeDatabase(tempName);
                return Result<RestoreReport, ApplicationError>::failure(
                    {ErrorCode::Storage, "backup cannot be opened", {}, false});
            }
            QSqlQuery integrity(check);
            integrity.exec(QStringLiteral("PRAGMA integrity_check"));
            bool okRow = false;
            if (integrity.next())
                okRow = integrity.value(0).toString() == QStringLiteral("ok");
            QSqlQuery version(check);
            version.exec(QStringLiteral("SELECT MAX(version) FROM app_meta"));
            int backupSchema = 0;
            if (version.next())
                backupSchema = version.value(0).toInt();
            check.close();
            QSqlDatabase::removeDatabase(tempName);
            if (!okRow)
                return Result<RestoreReport, ApplicationError>::failure(
                    {ErrorCode::Storage, "backup integrity check failed", {}, false});
            // 与调用方传入的当前支持版本比较(此前硬编码 v6,应用升到 v9 后
            // 会错误拒绝所有新版备份——真机隐患排查发现)
            if (backupSchema > supportedSchemaVersion)
                return Result<RestoreReport, ApplicationError>::failure(
                    {ErrorCode::Conflict,
                     "backup schema newer than this app version supports", {}, false});
        }
    }

    // 4) 整体切换（实现负责：关闭连接→恢复前快照→换入→重开迁移→失败回退）
    std::string snapshotPath;
    const auto switched = m_switcher.switchTo(record->relativePath, &snapshotPath);
    if (!switched)
        return Result<RestoreReport, ApplicationError>::failure(switched.error());
    report.switched = true;
    report.preRestoreSnapshotPath = snapshotPath;

    // 5) 切换后旧连接已失效：备份记录状态与恢复审计由调用方用新连接追加
    //    （本服务返回报告，含恢复前安全快照路径；审计事件样例见 tst_restore）。
    return Result<RestoreReport, ApplicationError>::success(report);
}

StartupRecovery::StartupRecovery(MelRepository &mels, MelUseCases &melUseCases,
                                 ReminderService &reminders, const Domain::Uid &userId,
                                 const Domain::Clock &clock)
    : m_mels(mels), m_melUseCases(melUseCases), m_reminders(reminders), m_userId(userId),
      m_clock(clock)
{}

Result<StartupRecovery::Report, ApplicationError> StartupRecovery::run()
{
    Report report;
    // 逾期 MEL 结算（settle 幂等：重复启动只结算一次，E2E-05）
    const auto due = m_mels.findDue(m_clock.utcIso(), 100);
    report.dueMelsFound = static_cast<int>(due.size());
    for (const auto &mel : due) {
        const auto overdue = m_melUseCases.markOverdue(mel.uid, mel.revision);
        if (!overdue)
            continue;
        const auto settled = m_melUseCases.settleMel(mel.uid, overdue.value().revision);
        if (settled)
            ++report.settled;
    }
    // 结算后再建默认规则（已结算的 MEL 不再自动建规则）；
    // 随后补发到期投递（幂等键保证同一次到期不重复）
    const auto ensured = m_reminders.ensureMelDeadlineReminders(m_userId, 100);
    if (!ensured)
        return Result<Report, ApplicationError>::failure(ensured.error());
    report.remindersCreated = ensured.value();
    const auto scheduled = m_reminders.scheduleDue(m_clock.utcIso());
    if (!scheduled)
        return Result<Report, ApplicationError>::failure(scheduled.error());
    report.remindersScheduled = scheduled.value();
    return Result<Report, ApplicationError>::success(report);
}

} // namespace PersonOS::Application
