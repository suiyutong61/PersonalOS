#include "infrastructure/persistence/SqlRouteRepository.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlRouteRepository::SqlRouteRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlRouteRepository::writeFailure(const char *operation,
                                                          const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<Domain::Route> SqlRouteRepository::findByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT r.uid, g.uid AS goal_uid, r.status, r.current_version_uid, r.created_by, "
        "r.revision, r.created_at, r.updated_at "
        "FROM routes_v3 r LEFT JOIN goals_v3 g ON g.id = r.goal_id WHERE r.uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto goalId = Domain::Uid::parse(query.value("goal_uid").toString().toStdString());
    if (!parsed || !goalId)
        return std::nullopt;
    Domain::Route route;
    route.uid = *parsed;
    route.goalId = *goalId;
    if (const auto status =
            Domain::routeStatusFrom(query.value("status").toString().toStdString()))
        route.status = *status;
    if (!query.value("current_version_uid").isNull())
        route.currentVersionUid = query.value("current_version_uid").toString().toStdString();
    route.createdBy = query.value("created_by").toString().toStdString();
    route.revision = query.value("revision").toInt();
    route.createdAt = query.value("created_at").toString().toStdString();
    route.updatedAt = query.value("updated_at").toString().toStdString();
    return route;
}

std::vector<Domain::Route> SqlRouteRepository::findByGoal(const Domain::Uid &goalId)
{
    std::vector<Domain::Route> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT r.uid, g.uid AS goal_uid, r.status, r.current_version_uid, r.created_by, "
        "r.revision, r.created_at, r.updated_at "
        "FROM routes_v3 r LEFT JOIN goals_v3 g ON g.id = r.goal_id "
        "WHERE r.goal_id=(SELECT id FROM goals_v3 WHERE uid=?) ORDER BY r.id"));
    query.addBindValue(QString::fromStdString(goalId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto parsed = Domain::Uid::parse(query.value("uid").toString().toStdString());
        const auto gid = Domain::Uid::parse(query.value("goal_uid").toString().toStdString());
        if (!parsed || !gid)
            continue;
        Domain::Route route;
        route.uid = *parsed;
        route.goalId = *gid;
        if (const auto status =
                Domain::routeStatusFrom(query.value("status").toString().toStdString()))
            route.status = *status;
        if (!query.value("current_version_uid").isNull())
            route.currentVersionUid = query.value("current_version_uid").toString().toStdString();
        route.createdBy = query.value("created_by").toString().toStdString();
        route.revision = query.value("revision").toInt();
        route.createdAt = query.value("created_at").toString().toStdString();
        route.updatedAt = query.value("updated_at").toString().toStdString();
        out.push_back(std::move(route));
    }
    return out;
}

Application::SaveResult SqlRouteRepository::insert(const Domain::Route &route)
{
    if (!route.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "route invalid", {}, false}};

    // 先在 C++ 侧解析外键 id，避免 SQL 子查询参数计数错误
    QSqlQuery resolve(m_database);
    resolve.prepare(QStringLiteral("SELECT id FROM goals_v3 WHERE uid=?"));
    resolve.addBindValue(QString::fromStdString(route.goalId.value()));
    if (!resolve.exec() || !resolve.next())
        return {false, false,
                {Application::ErrorCode::NotFound,
                 "goal not found for route", {}, false}};
    const qint64 goalPk = resolve.value(0).toLongLong();

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO routes_v3(uid, goal_id, status, current_version_uid, created_by, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(route.uid.value()));
    query.addBindValue(goalPk);
    query.addBindValue(QString::fromStdString(Domain::toString(route.status)));
    query.addBindValue(route.currentVersionUid
                           ? QVariant(QString::fromStdString(*route.currentVersionUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(route.createdBy));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("route insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlRouteRepository::update(const Domain::Route &route, int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE routes_v3 SET status=?, current_version_uid=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(route.status)));
    query.addBindValue(route.currentVersionUid
                           ? QVariant(QString::fromStdString(*route.currentVersionUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(route.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("route update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "route revision conflict", {}, false}};
    return {true, false, {}};
}

Application::Result<std::string, Application::ApplicationError> SqlRouteRepository::insertVersion(
    const Domain::Uid &routeId, const Domain::RouteVersion &version)
{
    if (!version.assumptionsJson.empty()
        && QJsonDocument::fromJson(QByteArray::fromStdString(version.assumptionsJson)).isNull())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "assumptions_json invalid", {}, false});

    QSqlQuery routeQuery(m_database);
    routeQuery.prepare(QStringLiteral("SELECT id FROM routes_v3 WHERE uid=?"));
    routeQuery.addBindValue(QString::fromStdString(routeId.value()));
    if (!routeQuery.exec() || !routeQuery.next())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "route not found", {}, false});
    const qint64 routePk = routeQuery.value(0).toLongLong();

    const std::string now = formatUtcIso(m_clock.now());
    const std::string versionUid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    // assumptions_json 带 json_valid CHECK：空串非法，默认 '[]'
    const std::string assumptions =
        version.assumptionsJson.empty() ? std::string("[]") : version.assumptionsJson;

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO route_versions_v3(uid, route_id, version_no, status, "
        "rationale, evidence_summary, assumptions_json, user_confirmed_at, valid_from, "
        "created_at, created_by) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(versionUid));
    query.addBindValue(routePk);
    query.addBindValue(version.versionNo);
    query.addBindValue(QStringLiteral("active"));
    query.addBindValue(QString::fromStdString(version.rationale));
    query.addBindValue(QString::fromStdString(version.evidenceSummary));
    query.addBindValue(QString::fromStdString(assumptions));
    query.addBindValue(version.userConfirmedAt
                           ? QVariant(QString::fromStdString(*version.userConfirmedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QStringLiteral("system"));
    if (!query.exec())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "route version insert failed",
             query.lastError().text().toStdString(), false});
    const qint64 versionPk = query.lastInsertId().toLongLong();

    // 阶段一次写入（同版本）；父阶段必须在同一版本内且先于子阶段出现。
    // completion_rule_json 带 json_valid CHECK：空串非法，默认 '{}'。
    for (const auto &stage : version.stages) {
        QVariant parentPk;
        if (stage.parentStageId) {
            QSqlQuery parentQuery(m_database);
            parentQuery.prepare(QStringLiteral(
                "SELECT id FROM route_stages_v3 WHERE uid=? AND route_version_id=?"));
            parentQuery.addBindValue(QString::fromStdString(stage.parentStageId->value()));
            parentQuery.addBindValue(versionPk);
            if (!parentQuery.exec() || !parentQuery.next())
                return Application::Result<std::string, Application::ApplicationError>::failure(
                    {Application::ErrorCode::Validation,
                     "stage parent not found in same version", {}, false});
            parentPk = parentQuery.value(0);
        }
        const std::string rule = stage.completionRuleJson.empty()
                                     ? std::string("{}")
                                     : stage.completionRuleJson;
        QSqlQuery stageQuery(m_database);
        stageQuery.prepare(QStringLiteral(
            "INSERT INTO route_stages_v3(uid, route_version_id, parent_stage_id, title, "
            "description, sequence_no, completion_rule_json, estimated_effort_min, status, "
            "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
        stageQuery.addBindValue(QString::fromStdString(stage.uid.value()));
        stageQuery.addBindValue(versionPk);
        stageQuery.addBindValue(parentPk);
        stageQuery.addBindValue(QString::fromStdString(stage.title));
        stageQuery.addBindValue(QString::fromStdString(stage.description));
        stageQuery.addBindValue(stage.sequenceNo);
        stageQuery.addBindValue(QString::fromStdString(rule));
        stageQuery.addBindValue(stage.estimatedEffortMin ? QVariant(*stage.estimatedEffortMin)
                                                         : QVariant());
        stageQuery.addBindValue(QStringLiteral("pending"));
        stageQuery.addBindValue(QString::fromStdString(now));
        stageQuery.addBindValue(QString::fromStdString(now));
        if (!stageQuery.exec())
            return Application::Result<std::string, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "route stage insert failed",
                 stageQuery.lastError().text().toStdString(), false});
    }
    return Application::Result<std::string, Application::ApplicationError>::success(versionUid);
}

Application::SaveResult SqlRouteRepository::markVersionConfirmed(const Domain::Uid &routeId,
                                                                  int versionNo,
                                                                  const std::string &confirmedAtIso)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE route_versions_v3 SET user_confirmed_at=? "
        "WHERE route_id=(SELECT id FROM routes_v3 WHERE uid=?) AND version_no=? "
        "AND user_confirmed_at IS NULL"));
    query.addBindValue(QString::fromStdString(confirmedAtIso));
    query.addBindValue(QString::fromStdString(routeId.value()));
    query.addBindValue(versionNo);
    if (!query.exec())
        return writeFailure("route version confirm failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict,
                 "version already confirmed or not found", {}, false}};
    return {true, false, {}};
}

std::vector<Domain::RouteVersion> SqlRouteRepository::versionsOf(const Domain::Uid &routeId)
{
    std::vector<Domain::RouteVersion> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, version_no, rationale, evidence_summary, assumptions_json, user_confirmed_at "
        "FROM route_versions_v3 WHERE route_id=(SELECT id FROM routes_v3 WHERE uid=?) "
        "ORDER BY version_no"));
    query.addBindValue(QString::fromStdString(routeId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::RouteVersion version;
        version.uid = query.value("uid").toString().toStdString();
        version.versionNo = query.value("version_no").toInt();
        version.rationale = query.value("rationale").toString().toStdString();
        version.evidenceSummary = query.value("evidence_summary").toString().toStdString();
        version.assumptionsJson = query.value("assumptions_json").toString().toStdString();
        if (!query.value("user_confirmed_at").isNull())
            version.userConfirmedAt = query.value("user_confirmed_at").toString().toStdString();
        out.push_back(std::move(version));
    }
    return out;
}

// ===== 阶段详情（route_stage_details_v11 / route_stage_materials_v11）=====

namespace {
// JSON 列带 json_valid CHECK：空串非法，仓储统一默认 '[]'（insertVersion 同款口径）。
std::string jsonArrayOrEmpty(const std::string &json)
{
    return json.empty() ? std::string("[]") : json;
}

bool jsonInvalidWhenNonEmpty(const std::string &json)
{
    return !json.empty() && QJsonDocument::fromJson(QByteArray::fromStdString(json)).isNull();
}
} // namespace

std::optional<Application::StageLocation> SqlRouteRepository::locateStage(
    const Domain::Uid &stageUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT s.uid, s.title, s.description, s.sequence_no, s.completion_rule_json, "
        "s.estimated_effort_min, s.revision, v.uid AS version_uid, v.version_no, "
        "r.uid AS route_uid, r.status "
        "FROM route_stages_v3 s "
        "JOIN route_versions_v3 v ON v.id=s.route_version_id "
        "JOIN routes_v3 r ON r.id=v.route_id WHERE s.uid=?"));
    query.addBindValue(QString::fromStdString(stageUid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto routeUid = Domain::Uid::parse(query.value("route_uid").toString().toStdString());
    if (!parsed || !routeUid)
        return std::nullopt;
    Application::StageLocation location;
    location.stage.uid = *parsed;
    location.stage.title = query.value("title").toString().toStdString();
    location.stage.description = query.value("description").toString().toStdString();
    location.stage.sequenceNo = query.value("sequence_no").toInt();
    location.stage.completionRuleJson =
        query.value("completion_rule_json").toString().toStdString();
    if (!query.value("estimated_effort_min").isNull())
        location.stage.estimatedEffortMin = query.value("estimated_effort_min").toInt();
    location.stage.revision = query.value("revision").toInt();
    location.routeUid = *routeUid;
    location.routeVersionUid = query.value("version_uid").toString().toStdString();
    location.routeVersionNo = query.value("version_no").toInt();
    if (const auto status =
            Domain::routeStatusFrom(query.value("status").toString().toStdString()))
        location.routeStatus = *status;
    return location;
}

std::vector<Domain::StageDetailVersion> SqlRouteRepository::stageDetailVersionsOf(
    const Domain::Uid &stageUid)
{
    std::vector<Domain::StageDetailVersion> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, version_no, outcomes_json, tasks_json, projects_json, criteria_json, "
        "rationale, created_by, user_confirmed_at, revision FROM route_stage_details_v11 "
        "WHERE stage_id=(SELECT id FROM route_stages_v3 WHERE uid=?) ORDER BY version_no"));
    query.addBindValue(QString::fromStdString(stageUid.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::StageDetailVersion detail;
        if (const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString()))
            detail.uid = *uid;
        detail.stageUid = stageUid;
        detail.versionNo = query.value("version_no").toInt();
        detail.outcomesJson = query.value("outcomes_json").toString().toStdString();
        detail.tasksJson = query.value("tasks_json").toString().toStdString();
        detail.projectsJson = query.value("projects_json").toString().toStdString();
        detail.criteriaJson = query.value("criteria_json").toString().toStdString();
        detail.rationale = query.value("rationale").toString().toStdString();
        detail.createdBy = query.value("created_by").toString().toStdString();
        if (!query.value("user_confirmed_at").isNull())
            detail.userConfirmedAt = query.value("user_confirmed_at").toString().toStdString();
        detail.revision = query.value("revision").toInt();
        out.push_back(std::move(detail));
    }
    return out;
}

Application::Result<std::string, Application::ApplicationError>
SqlRouteRepository::insertStageDetail(const Domain::StageDetailVersion &detail)
{
    if (jsonInvalidWhenNonEmpty(detail.outcomesJson) || jsonInvalidWhenNonEmpty(detail.tasksJson)
        || jsonInvalidWhenNonEmpty(detail.projectsJson)
        || jsonInvalidWhenNonEmpty(detail.criteriaJson))
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "stage detail json invalid", {}, false});

    QSqlQuery stageQuery(m_database);
    stageQuery.prepare(QStringLiteral("SELECT id FROM route_stages_v3 WHERE uid=?"));
    stageQuery.addBindValue(QString::fromStdString(detail.stageUid.value()));
    if (!stageQuery.exec() || !stageQuery.next())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "route stage not found", {}, false});
    const qint64 stagePk = stageQuery.value(0).toLongLong();

    const std::string now = formatUtcIso(m_clock.now());
    const std::string uid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO route_stage_details_v11(uid, stage_id, version_no, outcomes_json, "
        "tasks_json, projects_json, criteria_json, rationale, created_by, created_at, "
        "user_confirmed_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(uid));
    query.addBindValue(stagePk);
    query.addBindValue(detail.versionNo);
    query.addBindValue(QString::fromStdString(jsonArrayOrEmpty(detail.outcomesJson)));
    query.addBindValue(QString::fromStdString(jsonArrayOrEmpty(detail.tasksJson)));
    query.addBindValue(QString::fromStdString(jsonArrayOrEmpty(detail.projectsJson)));
    query.addBindValue(QString::fromStdString(jsonArrayOrEmpty(detail.criteriaJson)));
    query.addBindValue(QString::fromStdString(detail.rationale));
    query.addBindValue(QString::fromStdString(detail.createdBy));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(detail.userConfirmedAt
                           ? QVariant(QString::fromStdString(*detail.userConfirmedAt))
                           : QVariant());
    if (!query.exec())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "stage detail insert failed",
             query.lastError().text().toStdString(), false});
    return Application::Result<std::string, Application::ApplicationError>::success(uid);
}

Application::SaveResult SqlRouteRepository::markStageDetailConfirmed(
    const Domain::Uid &stageUid, int versionNo, const std::string &confirmedAtIso)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE route_stage_details_v11 SET user_confirmed_at=? "
        "WHERE stage_id=(SELECT id FROM route_stages_v3 WHERE uid=?) AND version_no=? "
        "AND user_confirmed_at IS NULL"));
    query.addBindValue(QString::fromStdString(confirmedAtIso));
    query.addBindValue(QString::fromStdString(stageUid.value()));
    query.addBindValue(versionNo);
    if (!query.exec())
        return writeFailure("stage detail confirm failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict,
                 "stage detail already confirmed or not found", {}, false}};
    return {true, false, {}};
}

Application::SaveResult SqlRouteRepository::upsertStageMaterial(
    const Domain::StageMaterialBinding &material)
{
    QSqlQuery stageQuery(m_database);
    stageQuery.prepare(QStringLiteral("SELECT id FROM route_stages_v3 WHERE uid=?"));
    stageQuery.addBindValue(QString::fromStdString(material.stageUid.value()));
    if (!stageQuery.exec() || !stageQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "route stage not found", {}, false}};
    const qint64 stagePk = stageQuery.value(0).toLongLong();

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    // 重建议语义：已 accepted 的用户决定保持不动，其余重置为 pending 并更新建议内容。
    // 未加限定的 user_choice 在 DO UPDATE 中指原行值；excluded.* 指本次插入值。
    query.prepare(QStringLiteral(
        "INSERT INTO route_stage_materials_v11(stage_id, knowledge_item_uid, "
        "knowledge_version_uid, rank, reason, user_choice, created_by, updated_at) "
        "VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(stage_id,knowledge_item_uid) DO UPDATE SET "
        "knowledge_version_uid=excluded.knowledge_version_uid, rank=excluded.rank, "
        "reason=excluded.reason, "
        "user_choice=CASE WHEN user_choice='accepted' THEN 'accepted' ELSE 'pending' END, "
        "created_by=excluded.created_by, updated_at=excluded.updated_at"));
    query.addBindValue(stagePk);
    query.addBindValue(QString::fromStdString(material.knowledgeItemUid));
    query.addBindValue(QString::fromStdString(material.knowledgeVersionUid));
    query.addBindValue(material.rank);
    query.addBindValue(QString::fromStdString(material.reason));
    query.addBindValue(QString::fromStdString(material.userChoice));
    query.addBindValue(QString::fromStdString(material.createdBy));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("stage material upsert failed", query);
    return {true, false, {}};
}

std::vector<Domain::StageMaterialBinding> SqlRouteRepository::stageMaterialsOf(
    const Domain::Uid &stageUid)
{
    std::vector<Domain::StageMaterialBinding> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT knowledge_item_uid, knowledge_version_uid, rank, reason, user_choice, "
        "created_by FROM route_stage_materials_v11 "
        "WHERE stage_id=(SELECT id FROM route_stages_v3 WHERE uid=?) ORDER BY rank, id"));
    query.addBindValue(QString::fromStdString(stageUid.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::StageMaterialBinding material;
        material.stageUid = stageUid;
        material.knowledgeItemUid = query.value("knowledge_item_uid").toString().toStdString();
        material.knowledgeVersionUid =
            query.value("knowledge_version_uid").toString().toStdString();
        material.rank = query.value("rank").toInt();
        material.reason = query.value("reason").toString().toStdString();
        material.userChoice = query.value("user_choice").toString().toStdString();
        material.createdBy = query.value("created_by").toString().toStdString();
        out.push_back(std::move(material));
    }
    return out;
}

Application::SaveResult SqlRouteRepository::updateStageMaterialChoice(
    const Domain::Uid &stageUid, const std::string &itemUid, const std::string &choice)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE route_stage_materials_v11 SET user_choice=?, updated_at=? "
        "WHERE stage_id=(SELECT id FROM route_stages_v3 WHERE uid=?) "
        "AND knowledge_item_uid=?"));
    query.addBindValue(QString::fromStdString(choice));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(stageUid.value()));
    query.addBindValue(QString::fromStdString(itemUid));
    if (!query.exec())
        return writeFailure("stage material choice update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, false,
                {Application::ErrorCode::NotFound, "stage material not found", {}, false}};
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
