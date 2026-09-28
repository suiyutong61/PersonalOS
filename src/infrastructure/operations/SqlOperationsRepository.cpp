#include "infrastructure/operations/SqlOperationsRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlOperationsRepository::SqlOperationsRepository(QSqlDatabase database,
                                                 const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlOperationsRepository::writeFailure(const char *operation,
                                                               const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<qint64> SqlOperationsRepository::resolvePk(const char *sql,
                                                         const std::string &uid) const
{
    QSqlQuery query(m_database);
    query.prepare(QString::fromLatin1(sql));
    query.addBindValue(QString::fromStdString(uid));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

Application::SaveResult SqlOperationsRepository::insertEvent(
    const Domain::CalendarEvent &event)
{
    if (!event.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "calendar event invalid", {}, false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO calendar_events_v6(uid, owner_type, owner_uid, starts_at, ends_at, "
        "timezone_id, status, external_ref, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(event.uid.value()));
    query.addBindValue(QString::fromStdString(event.ownerType));
    query.addBindValue(QString::fromStdString(event.ownerUid));
    query.addBindValue(QString::fromStdString(event.startsAt));
    query.addBindValue(QString::fromStdString(event.endsAt));
    query.addBindValue(QString::fromStdString(event.timezoneId));
    query.addBindValue(QString::fromStdString(event.status));
    query.addBindValue(event.externalRef
                           ? QVariant(QString::fromStdString(*event.externalRef))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("calendar event insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::CalendarEvent> SqlOperationsRepository::findEvent(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, owner_type, owner_uid, starts_at, ends_at, timezone_id, status, "
        "external_ref, revision FROM calendar_events_v6 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::CalendarEvent event;
    event.uid = *parsed;
    event.ownerType = query.value(1).toString().toStdString();
    event.ownerUid = query.value(2).toString().toStdString();
    event.startsAt = query.value(3).toString().toStdString();
    event.endsAt = query.value(4).toString().toStdString();
    event.timezoneId = query.value(5).toString().toStdString();
    event.status = query.value(6).toString().toStdString();
    if (!query.value(7).isNull())
        event.externalRef = query.value(7).toString().toStdString();
    event.revision = query.value(8).toInt();
    return event;
}

std::optional<Domain::ReminderRule> SqlOperationsRepository::findRule(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, owner_type, owner_uid, offset_min, channel, enabled, quiet_hours_json, "
        "snooze_policy_json, revision FROM reminder_rules_v6 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::ReminderRule rule;
    rule.uid = *parsed;
    rule.ownerType = query.value(1).toString().toStdString();
    rule.ownerUid = query.value(2).toString().toStdString();
    rule.offsetMin = query.value(3).toInt();
    rule.channel = query.value(4).toString().toStdString();
    rule.enabled = query.value(5).toInt() != 0;
    rule.quietHoursJson = query.value(6).toString().toStdString();
    rule.snoozePolicyJson = query.value(7).toString().toStdString();
    rule.revision = query.value(8).toInt();
    return rule;
}

Application::SaveResult SqlOperationsRepository::insertRule(const Domain::ReminderRule &rule)
{
    if (!rule.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "reminder rule invalid", {}, false}};
    std::optional<qint64> eventPk;
    if (rule.eventUid) {
        eventPk = resolvePk("SELECT id FROM calendar_events_v6 WHERE uid=?", *rule.eventUid);
        if (!eventPk)
            return {false, false,
                    {Application::ErrorCode::NotFound, "calendar event not found", {},
                     false}};
    }
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminder_rules_v6(uid, event_id, owner_type, owner_uid, offset_min, "
        "channel, enabled, quiet_hours_json, snooze_policy_json, created_at, updated_at, "
        "revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(rule.uid.value()));
    query.addBindValue(eventPk ? QVariant(*eventPk) : QVariant());
    query.addBindValue(QString::fromStdString(rule.ownerType));
    query.addBindValue(QString::fromStdString(rule.ownerUid));
    query.addBindValue(rule.offsetMin);
    query.addBindValue(QString::fromStdString(rule.channel));
    query.addBindValue(rule.enabled ? 1 : 0);
    query.addBindValue(QString::fromStdString(rule.quietHoursJson));
    query.addBindValue(QString::fromStdString(rule.snoozePolicyJson));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("reminder rule insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlOperationsRepository::updateRule(const Domain::ReminderRule &rule,
                                                            int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE reminder_rules_v6 SET offset_min=?, channel=?, enabled=?, "
        "quiet_hours_json=?, snooze_policy_json=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(rule.offsetMin);
    query.addBindValue(QString::fromStdString(rule.channel));
    query.addBindValue(rule.enabled ? 1 : 0);
    query.addBindValue(QString::fromStdString(rule.quietHoursJson));
    query.addBindValue(QString::fromStdString(rule.snoozePolicyJson));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(rule.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("reminder rule update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "reminder rule revision conflict", {},
                 false}};
    return {true, false, {}};
}

std::vector<Domain::ReminderRule> SqlOperationsRepository::enabledRules()
{
    std::vector<Domain::ReminderRule> out;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT uid FROM reminder_rules_v6 WHERE enabled=1 ORDER BY id")))
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto rule = findRule(*uid))
            out.push_back(*rule);
    }
    return out;
}

Application::SaveResult SqlOperationsRepository::insertDelivery(
    const Domain::ReminderDelivery &delivery)
{
    if (!delivery.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "reminder delivery invalid", {}, false}};
    const auto rulePk =
        resolvePk("SELECT id FROM reminder_rules_v6 WHERE uid=?", delivery.ruleUid.value());
    if (!rulePk)
        return {false, false,
                {Application::ErrorCode::NotFound, "reminder rule not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reminder_deliveries_v6(uid, rule_id, scheduled_at, delivered_at, "
        "status, error, idempotency_key) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(delivery.uid.value()));
    query.addBindValue(*rulePk);
    query.addBindValue(QString::fromStdString(delivery.scheduledAt));
    query.addBindValue(delivery.deliveredAt
                           ? QVariant(QString::fromStdString(*delivery.deliveredAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(delivery.status));
    query.addBindValue(delivery.error
                           ? QVariant(QString::fromStdString(*delivery.error))
                           : QVariant());
    query.addBindValue(QString::fromStdString(delivery.idempotencyKey));
    if (!query.exec())
        return writeFailure("reminder delivery insert failed", query);
    return {true, false, {}};
}

bool SqlOperationsRepository::existsDeliveryKey(const std::string &idempotencyKey)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM reminder_deliveries_v6 WHERE idempotency_key=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    return query.exec() && query.next();
}

std::vector<Domain::ReminderDelivery> SqlOperationsRepository::pendingDeliveries(
    const std::string &nowIso)
{
    std::vector<Domain::ReminderDelivery> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, scheduled_at, delivered_at, status, error, idempotency_key "
        "FROM reminder_deliveries_v6 WHERE status='pending' AND scheduled_at<=? ORDER BY "
        "scheduled_at"));
    query.addBindValue(QString::fromStdString(nowIso));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        Domain::ReminderDelivery delivery;
        delivery.uid = *uid;
        delivery.scheduledAt = query.value(1).toString().toStdString();
        if (!query.value(2).isNull())
            delivery.deliveredAt = query.value(2).toString().toStdString();
        delivery.status = query.value(3).toString().toStdString();
        if (!query.value(4).isNull())
            delivery.error = query.value(4).toString().toStdString();
        delivery.idempotencyKey = query.value(5).toString().toStdString();
        out.push_back(std::move(delivery));
    }
    return out;
}

Application::SaveResult SqlOperationsRepository::markDelivery(
    const Domain::Uid &uid, const std::string &status,
    const std::optional<std::string> &error)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE reminder_deliveries_v6 SET status=?, delivered_at=?, error=? WHERE uid=?"));
    query.addBindValue(QString::fromStdString(status));
    query.addBindValue(status == "delivered"
                           ? QVariant(QString::fromStdString(formatUtcIso(m_clock.now())))
                           : QVariant());
    query.addBindValue(error ? QVariant(QString::fromStdString(*error)) : QVariant());
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec())
        return writeFailure("reminder delivery update failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlOperationsRepository::insert(const Domain::Achievement &achievement)
{
    if (!achievement.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "achievement invalid", {}, false}};
    const auto userPk =
        resolvePk("SELECT id FROM user_profiles_v3 WHERE uid=?", achievement.userId.value());
    if (!userPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO achievements_v6(uid, user_id, achievement_type, title, description, "
        "earned_at, source_type, source_uid, evidence_json, created_at, updated_at, "
        "revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(achievement.uid.value()));
    query.addBindValue(*userPk);
    query.addBindValue(QString::fromStdString(achievement.achievementType));
    query.addBindValue(QString::fromStdString(achievement.title));
    query.addBindValue(QString::fromStdString(achievement.description));
    query.addBindValue(QString::fromStdString(achievement.earnedAt));
    query.addBindValue(QString::fromStdString(achievement.sourceType));
    query.addBindValue(QString::fromStdString(achievement.sourceUid));
    query.addBindValue(QString::fromStdString(achievement.evidenceJson));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("achievement insert failed", query);
    return {true, false, {}};
}

bool SqlOperationsRepository::exists(const Domain::Uid &userId, const std::string &type,
                                     const std::string &sourceType,
                                     const std::string &sourceUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM achievements_v6 WHERE user_id=(SELECT id FROM user_profiles_v3 "
        "WHERE uid=?) AND achievement_type=? AND source_type=? AND source_uid=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(type));
    query.addBindValue(QString::fromStdString(sourceType));
    query.addBindValue(QString::fromStdString(sourceUid));
    return query.exec() && query.next();
}

std::vector<Domain::Achievement> SqlOperationsRepository::listForUser(const Domain::Uid &userId,
                                                                      int limit)
{
    std::vector<Domain::Achievement> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, achievement_type, title, description, earned_at, source_type, "
        "source_uid, evidence_json, revision FROM achievements_v6 "
        "WHERE user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "ORDER BY earned_at DESC, id DESC LIMIT ?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Domain::Achievement achievement;
        achievement.uid = *uid;
        achievement.userId = userId;
        achievement.achievementType = query.value("achievement_type").toString().toStdString();
        achievement.title = query.value("title").toString().toStdString();
        achievement.description = query.value("description").toString().toStdString();
        achievement.earnedAt = query.value("earned_at").toString().toStdString();
        achievement.sourceType = query.value("source_type").toString().toStdString();
        achievement.sourceUid = query.value("source_uid").toString().toStdString();
        achievement.evidenceJson = query.value("evidence_json").toString().toStdString();
        achievement.revision = query.value("revision").toInt();
        out.push_back(std::move(achievement));
    }
    return out;
}

std::optional<Domain::BackupRecord> SqlOperationsRepository::find(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, started_at, completed_at, status, relative_path, sha256, "
        "db_schema_version, asset_manifest_json, error_json FROM backup_records_v6 "
        "WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::BackupRecord record;
    record.uid = *parsed;
    record.startedAt = query.value(1).toString().toStdString();
    if (!query.value(2).isNull())
        record.completedAt = query.value(2).toString().toStdString();
    record.status = query.value(3).toString().toStdString();
    record.relativePath = query.value(4).toString().toStdString();
    if (!query.value(5).isNull())
        record.sha256 = query.value(5).toString().toStdString();
    record.dbSchemaVersion = query.value(6).toInt();
    record.assetManifestJson = query.value(7).toString().toStdString();
    if (!query.value(8).isNull())
        record.errorJson = query.value(8).toString().toStdString();
    return record;
}

std::vector<Domain::BackupRecord> SqlOperationsRepository::list(int limit)
{
    std::vector<Domain::BackupRecord> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM backup_records_v6 ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto record = find(*uid))
            out.push_back(*record);
    }
    return out;
}

Application::SaveResult SqlOperationsRepository::insert(const Domain::BackupRecord &record)
{
    if (!record.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "backup record invalid", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO backup_records_v6(uid, started_at, completed_at, status, "
        "relative_path, sha256, db_schema_version, asset_manifest_json, error_json) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(record.uid.value()));
    query.addBindValue(QString::fromStdString(record.startedAt));
    query.addBindValue(record.completedAt
                           ? QVariant(QString::fromStdString(*record.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(record.status));
    query.addBindValue(QString::fromStdString(record.relativePath));
    query.addBindValue(record.sha256
                           ? QVariant(QString::fromStdString(*record.sha256))
                           : QVariant());
    query.addBindValue(record.dbSchemaVersion);
    query.addBindValue(QString::fromStdString(record.assetManifestJson));
    query.addBindValue(record.errorJson
                           ? QVariant(QString::fromStdString(*record.errorJson))
                           : QVariant());
    if (!query.exec())
        return writeFailure("backup record insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlOperationsRepository::update(const Domain::BackupRecord &record)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE backup_records_v6 SET completed_at=?, status=?, sha256=?, error_json=? "
        "WHERE uid=?"));
    query.addBindValue(record.completedAt
                           ? QVariant(QString::fromStdString(*record.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(record.status));
    query.addBindValue(record.sha256
                           ? QVariant(QString::fromStdString(*record.sha256))
                           : QVariant());
    query.addBindValue(record.errorJson
                           ? QVariant(QString::fromStdString(*record.errorJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(record.uid.value()));
    if (!query.exec())
        return writeFailure("backup record update failed", query);
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
