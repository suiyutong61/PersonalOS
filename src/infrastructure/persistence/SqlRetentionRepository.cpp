#include "infrastructure/persistence/SqlRetentionRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

const char *kColumns =
    "uid, next_due_at, interval_min, stability, difficulty, algorithm_code, "
    "algorithm_version, last_result_uid, active, revision";

std::optional<Domain::RetentionSchedule> scheduleFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::RetentionSchedule schedule;
    schedule.uid = *uid;
    schedule.nextDueAt = query.value("next_due_at").toString().toStdString();
    schedule.intervalMin = query.value("interval_min").toInt();
    if (!query.value("stability").isNull())
        schedule.stability = query.value("stability").toDouble();
    if (!query.value("difficulty").isNull())
        schedule.difficulty = query.value("difficulty").toDouble();
    schedule.algorithmCode = query.value("algorithm_code").toString().toStdString();
    schedule.algorithmVersion = query.value("algorithm_version").toString().toStdString();
    if (!query.value("last_result_uid").isNull())
        schedule.lastResultUid = query.value("last_result_uid").toString().toStdString();
    schedule.active = query.value("active").toInt() != 0;
    schedule.revision = query.value("revision").toInt();
    return schedule;
}

} // namespace

SqlRetentionRepository::SqlRetentionRepository(QSqlDatabase database,
                                               const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlRetentionRepository::writeFailure(const char *operation,
                                                              const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<Domain::RetentionSchedule> SqlRetentionRepository::findByUid(
    const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1 FROM retention_schedules_v4 WHERE uid=?")
                      .arg(QLatin1String(kColumns)));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return scheduleFromQuery(query);
}

Application::SaveResult SqlRetentionRepository::insert(
    const Domain::RetentionSchedule &schedule)
{
    if (!schedule.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "retention schedule invalid", {}, false}};

    QSqlQuery userQuery(m_database);
    userQuery.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    userQuery.addBindValue(QString::fromStdString(schedule.userId.value()));
    if (!userQuery.exec() || !userQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};
    const qint64 userId = userQuery.value(0).toLongLong();

    QVariant nodeId;
    if (schedule.contentNodeId) {
        QSqlQuery nodeQuery(m_database);
        nodeQuery.prepare(QStringLiteral("SELECT id FROM content_nodes_v3 WHERE uid=?"));
        nodeQuery.addBindValue(QString::fromStdString(schedule.contentNodeId->value()));
        if (!nodeQuery.exec() || !nodeQuery.next())
            return {false, false,
                    {Application::ErrorCode::NotFound, "content node not found", {}, false}};
        nodeId = nodeQuery.value(0).toLongLong();
    }
    QVariant itemId;
    if (schedule.assessmentItemId) {
        QSqlQuery itemQuery(m_database);
        itemQuery.prepare(QStringLiteral("SELECT id FROM assessment_items_v4 WHERE uid=?"));
        itemQuery.addBindValue(QString::fromStdString(schedule.assessmentItemId->value()));
        if (!itemQuery.exec() || !itemQuery.next())
            return {false, false,
                    {Application::ErrorCode::NotFound, "assessment item not found", {}, false}};
        itemId = itemQuery.value(0).toLongLong();
    }

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO retention_schedules_v4(uid, user_id, content_node_id, "
        "assessment_item_id, next_due_at, interval_min, stability, difficulty, "
        "algorithm_code, algorithm_version, last_result_uid, active, created_at, "
        "updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(schedule.uid.value()));
    query.addBindValue(userId);
    query.addBindValue(nodeId);
    query.addBindValue(itemId);
    query.addBindValue(QString::fromStdString(schedule.nextDueAt));
    query.addBindValue(schedule.intervalMin);
    query.addBindValue(schedule.stability
                           ? QVariant(*schedule.stability)
                           : QVariant());
    query.addBindValue(schedule.difficulty
                           ? QVariant(*schedule.difficulty)
                           : QVariant());
    query.addBindValue(QString::fromStdString(schedule.algorithmCode));
    query.addBindValue(QString::fromStdString(schedule.algorithmVersion));
    query.addBindValue(schedule.lastResultUid
                           ? QVariant(QString::fromStdString(*schedule.lastResultUid))
                           : QVariant());
    query.addBindValue(schedule.active ? 1 : 0);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("retention schedule insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlRetentionRepository::update(
    const Domain::RetentionSchedule &schedule, int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE retention_schedules_v4 SET next_due_at=?, interval_min=?, stability=?, "
        "difficulty=?, last_result_uid=?, active=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(schedule.nextDueAt));
    query.addBindValue(schedule.intervalMin);
    query.addBindValue(schedule.stability ? QVariant(*schedule.stability) : QVariant());
    query.addBindValue(schedule.difficulty ? QVariant(*schedule.difficulty) : QVariant());
    query.addBindValue(schedule.lastResultUid
                           ? QVariant(QString::fromStdString(*schedule.lastResultUid))
                           : QVariant());
    query.addBindValue(schedule.active ? 1 : 0);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(schedule.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("retention schedule update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "retention schedule revision conflict",
                 {}, false}};
    return {true, false, {}};
}

std::vector<Application::RetentionCandidate> SqlRetentionRepository::dueCandidates(
    const Domain::Uid &userId, const std::string &nowIso, int limit)
{
    std::vector<Application::RetentionCandidate> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT s.uid, s.next_due_at, s.interval_min, s.stability, s.difficulty, "
        "s.algorithm_code, s.algorithm_version, s.last_result_uid, s.active, s.revision, "
        "ar.mastery AS last_mastery, cn.weight AS node_weight "
        "FROM retention_schedules_v4 s "
        "LEFT JOIN assessment_results_v4 ar ON ar.uid=s.last_result_uid "
        "LEFT JOIN content_nodes_v3 cn ON cn.id=s.content_node_id "
        "WHERE s.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) AND s.active=1 "
        "AND s.next_due_at<=? ORDER BY s.next_due_at LIMIT ?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(nowIso));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto schedule = scheduleFromQuery(query);
        if (!schedule)
            continue;
        Application::RetentionCandidate candidate;
        candidate.schedule = *schedule;
        candidate.schedule.userId = userId;
        if (!query.value("last_mastery").isNull()) {
            if (const auto mastery = Domain::masteryFrom(
                    query.value("last_mastery").toString().toStdString()))
                candidate.lastMastery = *mastery;
        }
        if (!query.value("node_weight").isNull())
            candidate.nodeWeight = query.value("node_weight").toDouble();
        out.push_back(std::move(candidate));
    }
    return out;
}

std::vector<Domain::RetentionSchedule> SqlRetentionRepository::listForUser(
    const Domain::Uid &userId)
{
    std::vector<Domain::RetentionSchedule> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM retention_schedules_v4 WHERE user_id="
        "(SELECT id FROM user_profiles_v3 WHERE uid=?)")
                      .arg(QLatin1String(kColumns)));
    query.addBindValue(QString::fromStdString(userId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        if (const auto schedule = scheduleFromQuery(query)) {
            auto value = *schedule;
            value.userId = userId;
            out.push_back(std::move(value));
        }
    }
    return out;
}

} // namespace PersonOS::Infrastructure
