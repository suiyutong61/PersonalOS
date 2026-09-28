#include "application/usecases/operations/OperationUseCases.h"

#include "application/audit/Audit.h"

#include <QCryptographicHash>
#include <QUuid>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>

namespace PersonOS::Application {

// ---------------------------------------------------------------- Reminder

ReminderService::ReminderService(ReminderRepositoryPort &repo,
                                 NotificationPort &notifications, UuidPort &uids,
                                 const Domain::Clock &clock)
    : m_repo(repo), m_notifications(notifications), m_uids(uids), m_clock(clock)
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

Result<int, ApplicationError> ReminderService::scheduleDue(const std::string &triggerAtIso)
{
    int created = 0;
    for (const auto &rule : m_repo.enabledRules()) {
        // 幂等键 = rule uid + 触发时刻；同一次启动补发不会重复（DB-06）
        const std::string key = "reminder:" + rule.uid.value() + ":" + triggerAtIso;
        if (m_repo.existsDeliveryKey(key))
            continue;
        Domain::ReminderDelivery delivery;
        delivery.uid = m_uids.next();
        delivery.ruleUid = rule.uid;
        delivery.scheduledAt = triggerAtIso;
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
    for (const auto &pending : m_repo.pendingDeliveries(m_clock.utcIso())) {
        const bool ok = m_notifications.deliver("Personal OS 提醒", "您有到期的提醒事项");
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
    const Domain::Uid &backupUid)
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
            if (backupSchema > 6)   // v6 = 当前支持的最高 schema（数据库设计 §7）
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
                                 ReminderService &reminders, const Domain::Clock &clock)
    : m_mels(mels), m_melUseCases(melUseCases), m_reminders(reminders), m_clock(clock)
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
    // 提醒补发（幂等键保证不重复）
    m_reminders.scheduleDue(m_clock.utcIso());
    return Result<Report, ApplicationError>::success(report);
}

} // namespace PersonOS::Application
