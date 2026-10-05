#include "infrastructure/persistence/SqlMelRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

// 读路径：外键 JOIN 回 uid，领域层只见 Uid
const char *kMelColumns =
    "m.uid, u.uid AS user_uid, g.uid AS goal_uid, rv.uid AS route_version_uid, "
    "mv.uid AS manifest_version_uid, m.title, m.state, m.planned_start_at, "
    "m.planned_end_at, m.timezone_id, m.settlement_mode, m.capacity_min, m.reserve_min, "
    "m.rationale, m.knowledge_snapshot_uid, m.confirmed_at, m.activated_at, m.closed_at, "
    "m.created_at, m.updated_at, m.revision";

const char *kMelFrom =
    " FROM mels_v4 m "
    "LEFT JOIN user_profiles_v3 u ON u.id = m.user_id "
    "LEFT JOIN goals_v3 g ON g.id = m.goal_id "
    "LEFT JOIN route_versions_v3 rv ON rv.id = m.route_version_id "
    "LEFT JOIN domain_manifest_versions_v3 mv ON mv.id = m.manifest_version_id ";

std::optional<Domain::Mel> melFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto userId = Domain::Uid::parse(query.value("user_uid").toString().toStdString());
    const auto goalId = Domain::Uid::parse(query.value("goal_uid").toString().toStdString());
    const auto manifestId =
        Domain::Uid::parse(query.value("manifest_version_uid").toString().toStdString());
    if (!uid || !userId || !goalId || !manifestId)
        return std::nullopt;
    Domain::Mel mel;
    mel.uid = *uid;
    mel.userId = *userId;
    mel.goalId = *goalId;
    mel.manifestVersionId = *manifestId;
    if (!query.value("route_version_uid").isNull()) {
        if (const auto route =
                Domain::Uid::parse(query.value("route_version_uid").toString().toStdString()))
            mel.routeVersionId = *route;
    }
    mel.title = query.value("title").toString().toStdString();
    if (const auto state = Domain::melStateFrom(query.value("state").toString().toStdString()))
        mel.state = *state;
    mel.plannedStartAt = query.value("planned_start_at").toString().toStdString();
    mel.plannedEndAt = query.value("planned_end_at").toString().toStdString();
    mel.timezoneId = query.value("timezone_id").toString().toStdString();
    mel.settlementMode = query.value("settlement_mode").toString().toStdString();
    mel.capacityMin = query.value("capacity_min").toInt();
    mel.reserveMin = query.value("reserve_min").toInt();
    mel.rationale = query.value("rationale").toString().toStdString();
    if (!query.value("knowledge_snapshot_uid").isNull())
        mel.knowledgeSnapshotUid =
            query.value("knowledge_snapshot_uid").toString().toStdString();
    if (!query.value("confirmed_at").isNull())
        mel.confirmedAt = query.value("confirmed_at").toString().toStdString();
    if (!query.value("activated_at").isNull())
        mel.activatedAt = query.value("activated_at").toString().toStdString();
    if (!query.value("closed_at").isNull())
        mel.closedAt = query.value("closed_at").toString().toStdString();
    mel.createdAt = query.value("created_at").toString().toStdString();
    mel.updatedAt = query.value("updated_at").toString().toStdString();
    mel.revision = query.value("revision").toInt();
    return mel;
}

} // namespace

SqlMelRepository::SqlMelRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlMelRepository::writeFailure(const char *operation,
                                                        const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

qint64 SqlMelRepository::resolveMelPk(const Domain::Uid &melId, bool *found) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM mels_v4 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec() || !query.next())
        return 0;
    if (found)
        *found = true;
    return query.value(0).toLongLong();
}

qint64 SqlMelRepository::resolveUserId(const Domain::Uid &userId, bool *found) const
{
    if (found)
        *found = false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    if (!query.exec() || !query.next())
        return 0;
    if (found)
        *found = true;
    return query.value(0).toLongLong();
}

std::optional<qint64> SqlMelRepository::resolvePk(const char *sql, const Domain::Uid &uid) const
{
    QSqlQuery query(m_database);
    query.prepare(QString::fromLatin1(sql));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

std::optional<Domain::Mel> SqlMelRepository::findByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1%2 WHERE m.uid=?").arg(kMelColumns, kMelFrom));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return melFromQuery(query);
}

Application::SaveResult SqlMelRepository::insert(const Domain::Mel &mel)
{
    if (!mel.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "mel invalid", {}, false}};
    bool userFound = false;
    const qint64 userPk = resolveUserId(mel.userId, &userFound);
    if (!userFound)
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};
    // 外键 id 解析
    auto resolvePk = [this](const char *sql, const Domain::Uid &uid) -> std::optional<qint64> {
        QSqlQuery q(m_database);
        q.prepare(QString::fromLatin1(sql));
        q.addBindValue(QString::fromStdString(uid.value()));
        if (!q.exec() || !q.next())
            return std::nullopt;
        return q.value(0).toLongLong();
    };
    const auto goalPk = resolvePk("SELECT id FROM goals_v3 WHERE uid=?", mel.goalId);
    const auto manifestPk =
        resolvePk("SELECT id FROM domain_manifest_versions_v3 WHERE uid=?", mel.manifestVersionId);
    if (!goalPk || !manifestPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "goal/manifest version not found", {}, false}};
    std::optional<qint64> routePk;
    if (mel.routeVersionId) {
        routePk = resolvePk("SELECT id FROM route_versions_v3 WHERE uid=?", *mel.routeVersionId);
        if (!routePk)
            return {false, false,
                    {Application::ErrorCode::NotFound, "route version not found", {}, false}};
    }

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO mels_v4(uid, user_id, goal_id, route_version_id, manifest_version_id, "
        "title, state, planned_start_at, planned_end_at, timezone_id, settlement_mode, "
        "capacity_min, reserve_min, rationale, knowledge_snapshot_uid, confirmed_at, "
        "activated_at, closed_at, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(mel.uid.value()));
    query.addBindValue(userPk);
    query.addBindValue(*goalPk);
    query.addBindValue(routePk ? QVariant(*routePk) : QVariant());
    query.addBindValue(*manifestPk);
    query.addBindValue(QString::fromStdString(mel.title));
    query.addBindValue(QString::fromStdString(Domain::toString(mel.state)));
    query.addBindValue(QString::fromStdString(mel.plannedStartAt));
    query.addBindValue(QString::fromStdString(mel.plannedEndAt));
    query.addBindValue(QString::fromStdString(mel.timezoneId));
    query.addBindValue(QString::fromStdString(mel.settlementMode));
    query.addBindValue(mel.capacityMin);
    query.addBindValue(mel.reserveMin);
    query.addBindValue(QString::fromStdString(mel.rationale));
    query.addBindValue(mel.knowledgeSnapshotUid
                           ? QVariant(QString::fromStdString(*mel.knowledgeSnapshotUid))
                           : QVariant());
    query.addBindValue(mel.confirmedAt
                           ? QVariant(QString::fromStdString(*mel.confirmedAt))
                           : QVariant());
    query.addBindValue(mel.activatedAt
                           ? QVariant(QString::fromStdString(*mel.activatedAt))
                           : QVariant());
    query.addBindValue(mel.closedAt ? QVariant(QString::fromStdString(*mel.closedAt))
                                    : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("mel insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlMelRepository::update(const Domain::Mel &mel, int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE mels_v4 SET state=?, confirmed_at=?, activated_at=?, closed_at=?, "
        "rationale=?, capacity_min=?, reserve_min=?, planned_start_at=?, planned_end_at=?, "
        "settlement_mode=?, updated_at=?, revision=revision+1 WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(mel.state)));
    query.addBindValue(mel.confirmedAt
                           ? QVariant(QString::fromStdString(*mel.confirmedAt))
                           : QVariant());
    query.addBindValue(mel.activatedAt
                           ? QVariant(QString::fromStdString(*mel.activatedAt))
                           : QVariant());
    query.addBindValue(mel.closedAt ? QVariant(QString::fromStdString(*mel.closedAt))
                                    : QVariant());
    query.addBindValue(QString::fromStdString(mel.rationale));
    query.addBindValue(mel.capacityMin);
    query.addBindValue(mel.reserveMin);
    query.addBindValue(QString::fromStdString(mel.plannedStartAt));
    query.addBindValue(QString::fromStdString(mel.plannedEndAt));
    query.addBindValue(QString::fromStdString(mel.settlementMode));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(mel.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("mel update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "mel revision conflict", {}, false}};
    return {true, false, {}};
}

Application::SaveResult SqlMelRepository::insertTask(const Domain::MelTask &task)
{
    if (!task.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "task invalid", {}, false}};
    bool melFound = false;
    const qint64 melPk = resolveMelPk(task.melId, &melFound);
    if (!melFound)
        return {false, false,
                {Application::ErrorCode::NotFound, "mel not found", {}, false}};

    QVariant parentPk;
    if (task.parentTaskId) {
        QSqlQuery parentQuery(m_database);
        parentQuery.prepare(QStringLiteral("SELECT id FROM mel_tasks_v4 WHERE uid=?"));
        parentQuery.addBindValue(QString::fromStdString(task.parentTaskId->value()));
        if (!parentQuery.exec() || !parentQuery.next())
            return {false, false,
                    {Application::ErrorCode::NotFound, "parent task not found", {}, false}};
        parentPk = parentQuery.value(0);
    }

    const std::string now = formatUtcIso(m_clock.now());
    const std::string rule =
        task.completionRuleJson.empty() ? std::string("{}") : task.completionRuleJson;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO mel_tasks_v4(uid, mel_id, parent_task_id, title, description, "
        "sequence_no, required, planned_effort_min, completion_rule_json, state, progress, "
        "completed_at, created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(task.uid.value()));
    query.addBindValue(melPk);
    query.addBindValue(parentPk);
    query.addBindValue(QString::fromStdString(task.title));
    query.addBindValue(QString::fromStdString(task.description));
    query.addBindValue(task.sequenceNo);
    query.addBindValue(task.required ? 1 : 0);
    query.addBindValue(task.plannedEffortMin);
    query.addBindValue(QString::fromStdString(rule));
    query.addBindValue(QString::fromStdString(Domain::toString(task.state)));
    query.addBindValue(task.progress);
    query.addBindValue(task.completedAt
                           ? QVariant(QString::fromStdString(*task.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("mel task insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlMelRepository::updateTask(const Domain::MelTask &task,
                                                     int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE mel_tasks_v4 SET state=?, progress=?, completed_at=?, title=?, "
        "description=?, planned_effort_min=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(task.state)));
    query.addBindValue(task.progress);
    query.addBindValue(task.completedAt
                           ? QVariant(QString::fromStdString(*task.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(task.title));
    query.addBindValue(QString::fromStdString(task.description));
    query.addBindValue(task.plannedEffortMin);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(task.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("mel task update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "task revision conflict", {}, false}};
    return {true, false, {}};
}

std::vector<Domain::MelTask> SqlMelRepository::tasksOf(const Domain::Uid &melId)
{
    std::vector<Domain::MelTask> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, title, description, sequence_no, required, planned_effort_min, "
        "completion_rule_json, state, progress, completed_at, revision "
        "FROM mel_tasks_v4 WHERE mel_id=(SELECT id FROM mels_v4 WHERE uid=?) "
        "ORDER BY sequence_no, id"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Domain::MelTask task;
        task.uid = *uid;
        task.melId = melId;
        task.title = query.value("title").toString().toStdString();
        task.description = query.value("description").toString().toStdString();
        task.sequenceNo = query.value("sequence_no").toInt();
        task.required = query.value("required").toInt() != 0;
        task.plannedEffortMin = query.value("planned_effort_min").toInt();
        task.completionRuleJson =
            query.value("completion_rule_json").toString().toStdString();
        if (const auto state =
                Domain::melTaskStateFrom(query.value("state").toString().toStdString()))
            task.state = *state;
        task.progress = query.value("progress").toDouble();
        if (!query.value("completed_at").isNull())
            task.completedAt = query.value("completed_at").toString().toStdString();
        task.revision = query.value("revision").toInt();
        out.push_back(std::move(task));
    }
    return out;
}

Application::SaveResult SqlMelRepository::appendTransition(
    const Domain::MelTransition &transition)
{
    bool melFound = false;
    const qint64 melPk = resolveMelPk(transition.melId, &melFound);
    if (!melFound)
        return {false, false,
                {Application::ErrorCode::NotFound, "mel not found", {}, false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO mel_transitions_v4(uid, mel_id, from_state, to_state, trigger, "
        "actor_type, actor_ref, reason, idempotency_key, occurred_at, mel_revision_after) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(transition.uid.value()));
    query.addBindValue(melPk);
    query.addBindValue(QString::fromStdString(Domain::toString(transition.fromState)));
    query.addBindValue(QString::fromStdString(Domain::toString(transition.toState)));
    query.addBindValue(QString::fromStdString(transition.trigger));
    query.addBindValue(QString::fromStdString(transition.actorType));
    query.addBindValue(transition.actorRef
                           ? QVariant(QString::fromStdString(*transition.actorRef))
                           : QVariant());
    query.addBindValue(transition.reason.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(transition.reason)));
    query.addBindValue(QString::fromStdString(transition.idempotencyKey));
    query.addBindValue(QString::fromStdString(transition.occurredAt));
    query.addBindValue(transition.melRevisionAfter);
    if (!query.exec())
        return writeFailure("mel transition append failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlMelRepository::appendProgressEvent(
    const Domain::Mel &mel, const Domain::MelTask &task, double amount,
    const std::string &note, const std::string &idempotencyKey,
    const std::string &unit, const std::string &actorType)
{
    bool userFound = false;
    const qint64 userPk = resolveUserId(mel.userId, &userFound);
    bool melFound = false;
    const qint64 melPk = resolveMelPk(mel.uid, &melFound);
    if (!userFound || !melFound)
        return {false, false,
                {Application::ErrorCode::NotFound, "user/mel not found", {}, false}};

    QSqlQuery taskQuery(m_database);
    taskQuery.prepare(QStringLiteral("SELECT id FROM mel_tasks_v4 WHERE uid=?"));
    taskQuery.addBindValue(QString::fromStdString(task.uid.value()));
    if (!taskQuery.exec() || !taskQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "task not found", {}, false}};
    const qint64 taskPk = taskQuery.value(0).toLongLong();

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO progress_events_v4(uid, user_id, mel_id, goal_id, task_id, event_type, "
        "amount, unit, note, evidence_asset_uid, occurred_at, recorded_at, actor_type, "
        "idempotency_key, supersedes_uid) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(
        QString::fromStdString(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()));
    query.addBindValue(userPk);
    query.addBindValue(melPk);
    const auto goalPk = resolvePk("SELECT id FROM goals_v3 WHERE uid=?", mel.goalId);
    if (!goalPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "goal not found", {}, false}};
    query.addBindValue(*goalPk);
    query.addBindValue(taskPk);
    query.addBindValue(QString::fromLatin1(
        unit == "progress" && amount >= 1.0 ? "completed"
        : (amount > 0.0 ? "incremented" : "started")));
    query.addBindValue(amount);
    query.addBindValue(QString::fromStdString(unit));
    query.addBindValue(note.empty() ? QVariant() : QVariant(QString::fromStdString(note)));
    query.addBindValue(QVariant()); // evidence_asset_uid
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(actorType));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    query.addBindValue(QVariant()); // supersedes_uid
    if (!query.exec())
        return writeFailure("progress event append failed", query);
    return {true, false, {}};
}

bool SqlMelRepository::existsIdempotencyKey(const std::string &key)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM ("
        "SELECT idempotency_key FROM mel_transitions_v4 WHERE idempotency_key=? "
        "UNION ALL "
        "SELECT idempotency_key FROM progress_events_v4 WHERE idempotency_key=?"
        ") LIMIT 1"));
    query.addBindValue(QString::fromStdString(key));
    query.addBindValue(QString::fromStdString(key));
    return query.exec() && query.next();
}

bool SqlMelRepository::hasTransition(const Domain::Uid &melId, const std::string &trigger)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM mel_transitions_v4 WHERE trigger=? "
        "AND mel_id=(SELECT id FROM mels_v4 WHERE uid=?) LIMIT 1"));
    query.addBindValue(QString::fromStdString(trigger));
    query.addBindValue(QString::fromStdString(melId.value()));
    return query.exec() && query.next();
}

std::vector<Domain::Mel> SqlMelRepository::findActive(const Domain::Uid &userId, int limit)
{
    std::vector<Domain::Mel> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1%2 WHERE m.state IN ('active','paused') "
        "AND m.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "ORDER BY m.planned_end_at LIMIT ?")
                      .arg(kMelColumns, kMelFrom));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next())
        if (const auto mel = melFromQuery(query))
            out.push_back(*mel);
    return out;
}

std::vector<Domain::Mel> SqlMelRepository::findByUser(const Domain::Uid &userId, int limit)
{
    std::vector<Domain::Mel> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1%2 WHERE m.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "ORDER BY m.id DESC LIMIT ?")
                      .arg(kMelColumns, kMelFrom));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next())
        if (const auto mel = melFromQuery(query))
            out.push_back(*mel);
    return out;
}

std::optional<std::string> SqlMelRepository::lastProgressAtIso(const Domain::Uid &melId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT MAX(occurred_at) FROM progress_events_v4 WHERE mel_id="
        "(SELECT id FROM mels_v4 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec() || !query.next() || query.value(0).isNull())
        return std::nullopt;
    return query.value(0).toString().toStdString();
}

int SqlMelRepository::actualMinutesOf(const Domain::Uid &melId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount),0) FROM progress_events_v4 WHERE mel_id="
        "(SELECT id FROM mels_v4 WHERE uid=?) AND unit='minutes'"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

Application::SaveResult SqlMelRepository::insertTaskMethod(const Domain::MelTaskMethod &method)
{
    if (!method.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "task method invalid", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO mel_task_methods_v5(mel_task_id, method_version_id, rank, reason, "
        "applicability_json, user_choice) "
        "VALUES((SELECT id FROM mel_tasks_v4 WHERE uid=?),"
        "(SELECT id FROM knowledge_versions_v5 WHERE uid=?),?,?,?,?)"));
    query.addBindValue(QString::fromStdString(method.taskUid.value()));
    query.addBindValue(QString::fromStdString(method.methodVersionUid));
    query.addBindValue(method.rank);
    query.addBindValue(QString::fromStdString(method.reason));
    query.addBindValue(QString::fromStdString(method.applicabilityJson));
    query.addBindValue(QString::fromStdString(method.userChoice));
    if (!query.exec())
        return writeFailure("task method insert failed", query);
    return {true, false, {}};
}

std::vector<Domain::MelTaskMethod> SqlMelRepository::taskMethodsOf(const Domain::Uid &melId)
{
    std::vector<Domain::MelTaskMethod> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.uid AS task_uid, v.uid AS version_uid, m.rank, m.reason, "
        "m.applicability_json, m.user_choice FROM mel_task_methods_v5 m "
        "JOIN mel_tasks_v4 t ON t.id=m.mel_task_id "
        "JOIN knowledge_versions_v5 v ON v.id=m.method_version_id "
        "WHERE t.mel_id=(SELECT id FROM mels_v4 WHERE uid=?) ORDER BY m.rank"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::MelTaskMethod method;
        if (const auto taskUid = Domain::Uid::parse(
                query.value("task_uid").toString().toStdString()))
            method.taskUid = *taskUid;
        method.methodVersionUid = query.value("version_uid").toString().toStdString();
        method.rank = query.value("rank").toInt();
        method.reason = query.value("reason").toString().toStdString();
        method.applicabilityJson = query.value("applicability_json").toString().toStdString();
        method.userChoice = query.value("user_choice").toString().toStdString();
        if (method.isValid())
            out.push_back(std::move(method));
    }
    return out;
}

Application::SaveResult SqlMelRepository::insertPrediction(const Domain::MelPrediction &prediction)
{
    if (!prediction.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "prediction invalid", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO mel_predictions_v4(uid, mel_id, predicted_completion, "
        "predicted_effort_min, risk_level, basis_json, created_at, superseded_at) "
        "VALUES(?, (SELECT id FROM mels_v4 WHERE uid=?), ?, ?, ?, ?, ?, NULL)"));
    query.addBindValue(QString::fromStdString(prediction.uid.value()));
    query.addBindValue(QString::fromStdString(prediction.melId.value()));
    query.addBindValue(prediction.predictedCompletion);
    query.addBindValue(prediction.predictedEffortMin);
    query.addBindValue(QString::fromStdString(prediction.riskLevel));
    query.addBindValue(QString::fromStdString(prediction.basisJson));
    query.addBindValue(QString::fromStdString(prediction.createdAt));
    if (!query.exec())
        return writeFailure("prediction insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::MelPrediction> SqlMelRepository::latestPredictionOf(
    const Domain::Uid &melId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, predicted_completion, predicted_effort_min, risk_level, basis_json, "
        "created_at, superseded_at FROM mel_predictions_v4 WHERE mel_id="
        "(SELECT id FROM mels_v4 WHERE uid=?) AND superseded_at IS NULL "
        "ORDER BY created_at DESC, id DESC LIMIT 1"));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::MelPrediction prediction;
    prediction.uid = *uid;
    prediction.melId = melId;
    prediction.predictedCompletion = query.value(1).toDouble();
    prediction.predictedEffortMin = query.value(2).toInt();
    prediction.riskLevel = query.value(3).toString().toStdString();
    prediction.basisJson = query.value(4).toString().toStdString();
    prediction.createdAt = query.value(5).toString().toStdString();
    if (!query.value(6).isNull())
        prediction.supersededAt = query.value(6).toString().toStdString();
    return prediction;
}

std::vector<Domain::Mel> SqlMelRepository::findDue(const std::string &nowIso, int limit)
{
    std::vector<Domain::Mel> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1%2 WHERE m.state IN ('active','paused') "
        "AND m.planned_end_at<=? ORDER BY m.planned_end_at LIMIT ?")
                      .arg(kMelColumns, kMelFrom));
    query.addBindValue(QString::fromStdString(nowIso));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next())
        if (const auto mel = melFromQuery(query))
            out.push_back(*mel);
    return out;
}

} // namespace PersonOS::Infrastructure
