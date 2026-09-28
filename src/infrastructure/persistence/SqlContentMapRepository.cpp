#include "infrastructure/persistence/SqlContentMapRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

std::optional<Domain::ContentMap> mapFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::ContentMap map;
    map.uid = *uid;
    map.title = query.value("title").toString().toStdString();
    map.sourceType = query.value("source_type").toString().toStdString();
    if (!query.value("source_ref").isNull())
        map.sourceRef = query.value("source_ref").toString().toStdString();
    if (const auto status =
            Domain::contentMapStatusFrom(query.value("status").toString().toStdString()))
        map.status = *status;
    map.revision = query.value("revision").toInt();
    return map;
}

std::optional<Domain::ContentNode> nodeFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::ContentNode node;
    node.uid = *uid;
    node.title = query.value("title").toString().toStdString();
    node.nodeType = query.value("node_type").toString().toStdString();
    node.sequenceNo = query.value("sequence_no").toInt();
    node.weight = query.value("weight").toDouble();
    node.locatorJson = query.value("locator_json").toString().toStdString();
    node.revision = query.value("revision").toInt();
    return node;
}

} // namespace

SqlContentMapRepository::SqlContentMapRepository(QSqlDatabase database,
                                                 const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlContentMapRepository::writeFailure(const char *operation,
                                                              const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<Domain::ContentMap> SqlContentMapRepository::findMapByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT m.uid, m.title, m.source_type, m.source_ref, m.status, m.revision, "
        "g.uid AS goal_uid FROM content_maps_v3 m JOIN goals_v3 g ON g.id=m.goal_id "
        "WHERE m.uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto map = mapFromQuery(query);
    if (!map)
        return std::nullopt;
    if (const auto goalUid =
            Domain::Uid::parse(query.value("goal_uid").toString().toStdString()))
        map->goalId = *goalUid;
    return map;
}

std::vector<Domain::ContentMap> SqlContentMapRepository::mapsOfGoal(const Domain::Uid &goalId)
{
    std::vector<Domain::ContentMap> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, title, source_type, source_ref, status, revision FROM content_maps_v3 "
        "WHERE goal_id=(SELECT id FROM goals_v3 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(goalId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        if (const auto map = mapFromQuery(query)) {
            auto value = *map;
            value.goalId = goalId;
            out.push_back(std::move(value));
        }
    }
    return out;
}

Application::SaveResult SqlContentMapRepository::insertMap(const Domain::ContentMap &map)
{
    if (!map.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "content map invalid", {}, false}};
    QSqlQuery goalQuery(m_database);
    goalQuery.prepare(QStringLiteral("SELECT id FROM goals_v3 WHERE uid=?"));
    goalQuery.addBindValue(QString::fromStdString(map.goalId.value()));
    if (!goalQuery.exec() || !goalQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "goal not found", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO content_maps_v3(uid, goal_id, title, source_type, source_ref, status, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(map.uid.value()));
    query.addBindValue(goalQuery.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(map.title));
    query.addBindValue(QString::fromStdString(map.sourceType));
    query.addBindValue(map.sourceRef ? QVariant(QString::fromStdString(*map.sourceRef))
                                     : QVariant());
    query.addBindValue(QString::fromStdString(Domain::toString(map.status)));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("content map insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlContentMapRepository::updateMap(const Domain::ContentMap &map,
                                                           int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE content_maps_v3 SET status=?, source_ref=?, updated_at=?, "
        "revision=revision+1 WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(map.status)));
    query.addBindValue(map.sourceRef ? QVariant(QString::fromStdString(*map.sourceRef))
                                     : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(map.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("content map update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "content map revision conflict", {},
                 false}};
    return {true, false, {}};
}

std::optional<Domain::ContentNode> SqlContentMapRepository::findNodeByUid(
    const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT n.uid, n.title, n.node_type, n.sequence_no, n.weight, n.locator_json, "
        "n.revision, m.uid AS map_uid, p.uid AS parent_uid FROM content_nodes_v3 n "
        "JOIN content_maps_v3 m ON m.id=n.content_map_id "
        "LEFT JOIN content_nodes_v3 p ON p.id=n.parent_node_id WHERE n.uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto node = nodeFromQuery(query);
    if (!node)
        return std::nullopt;
    if (const auto mapUid = Domain::Uid::parse(query.value("map_uid").toString().toStdString()))
        node->mapId = *mapUid;
    if (!query.value("parent_uid").isNull())
        if (const auto parentUid =
                Domain::Uid::parse(query.value("parent_uid").toString().toStdString()))
            node->parentNodeId = *parentUid;
    return node;
}

std::vector<Domain::ContentNode> SqlContentMapRepository::nodesOfMap(const Domain::Uid &mapId)
{
    std::vector<Domain::ContentNode> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT n.uid, n.title, n.node_type, n.sequence_no, n.weight, n.locator_json, "
        "n.revision, p.uid AS parent_uid FROM content_nodes_v3 n "
        "LEFT JOIN content_nodes_v3 p ON p.id=n.parent_node_id "
        "WHERE n.content_map_id=(SELECT id FROM content_maps_v3 WHERE uid=?) "
        "ORDER BY n.sequence_no"));
    query.addBindValue(QString::fromStdString(mapId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        auto node = nodeFromQuery(query);
        if (!node)
            continue;
        node->mapId = mapId;
        if (!query.value("parent_uid").isNull())
            if (const auto parentUid = Domain::Uid::parse(
                    query.value("parent_uid").toString().toStdString()))
                node->parentNodeId = *parentUid;
        out.push_back(std::move(*node));
    }
    return out;
}

Application::SaveResult SqlContentMapRepository::insertNode(const Domain::ContentNode &node)
{
    if (!node.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "content node invalid", {}, false}};
    QSqlQuery mapQuery(m_database);
    mapQuery.prepare(QStringLiteral("SELECT id FROM content_maps_v3 WHERE uid=?"));
    mapQuery.addBindValue(QString::fromStdString(node.mapId.value()));
    if (!mapQuery.exec() || !mapQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "content map not found", {}, false}};

    QVariant parentId;
    if (node.parentNodeId) {
        QSqlQuery parentQuery(m_database);
        parentQuery.prepare(QStringLiteral(
            "SELECT id FROM content_nodes_v3 WHERE uid=? AND content_map_id=?"));
        parentQuery.addBindValue(QString::fromStdString(node.parentNodeId->value()));
        parentQuery.addBindValue(mapQuery.value(0).toLongLong());
        if (!parentQuery.exec() || !parentQuery.next())
            return {false, false,
                    {Application::ErrorCode::NotFound, "parent node not found in map", {},
                     false}};
        parentId = parentQuery.value(0).toLongLong();
    }

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO content_nodes_v3(uid, content_map_id, parent_node_id, title, node_type, "
        "sequence_no, weight, locator_json, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(node.uid.value()));
    query.addBindValue(mapQuery.value(0).toLongLong());
    query.addBindValue(parentId);
    query.addBindValue(QString::fromStdString(node.title));
    query.addBindValue(QString::fromStdString(node.nodeType));
    query.addBindValue(node.sequenceNo);
    query.addBindValue(node.weight);
    query.addBindValue(QString::fromStdString(node.locatorJson));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("content node insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlContentMapRepository::upsertProgress(
    const Domain::ContentProgress &progress)
{
    if (!progress.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "content progress invalid", {}, false}};
    QSqlQuery userQuery(m_database);
    userQuery.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    userQuery.addBindValue(QString::fromStdString(progress.userId.value()));
    if (!userQuery.exec() || !userQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};

    QSqlQuery nodeQuery(m_database);
    nodeQuery.prepare(QStringLiteral("SELECT id FROM content_nodes_v3 WHERE uid=?"));
    nodeQuery.addBindValue(QString::fromStdString(progress.nodeId.value()));
    if (!nodeQuery.exec() || !nodeQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "content node not found", {}, false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO content_progress_v3(content_node_id, user_id, state, progress, "
        "updated_at, source_event_uid) VALUES(?,?,?,?,?,?) "
        "ON CONFLICT(content_node_id,user_id) DO UPDATE SET state=excluded.state, "
        "progress=excluded.progress, updated_at=excluded.updated_at, "
        "source_event_uid=excluded.source_event_uid"));
    query.addBindValue(nodeQuery.value(0).toLongLong());
    query.addBindValue(userQuery.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(Domain::toString(progress.state)));
    query.addBindValue(progress.progress);
    query.addBindValue(QString::fromStdString(progress.updatedAt));
    query.addBindValue(progress.sourceEventUid
                           ? QVariant(QString::fromStdString(*progress.sourceEventUid))
                           : QVariant());
    if (!query.exec())
        return writeFailure("content progress upsert failed", query);
    return {true, false, {}};
}

std::vector<Domain::ContentProgressRow> SqlContentMapRepository::progressRowsOfMap(
    const Domain::Uid &mapId, const Domain::Uid &userId)
{
    std::vector<Domain::ContentProgressRow> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT n.uid AS node_uid, n.weight, "
        "COALESCE(p.state,'not_started') AS state, COALESCE(p.progress,0.0) AS progress "
        "FROM content_nodes_v3 n "
        "LEFT JOIN content_progress_v3 p ON p.content_node_id=n.id "
        "AND p.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "WHERE n.content_map_id=(SELECT id FROM content_maps_v3 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(mapId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::ContentProgressRow row;
        if (const auto nodeUid =
                Domain::Uid::parse(query.value("node_uid").toString().toStdString()))
            row.nodeId = *nodeUid;
        else
            continue;
        row.weight = query.value("weight").toDouble();
        if (const auto state = Domain::contentNodeStateFrom(
                query.value("state").toString().toStdString()))
            row.state = *state;
        row.progress = query.value("progress").toDouble();
        out.push_back(std::move(row));
    }
    return out;
}

Application::SaveResult SqlContentMapRepository::appendProgressEvent(
    const Domain::Uid &eventUid, const Domain::Uid &userId, const Domain::Uid &goalId,
    const Domain::Uid &nodeUid, const std::string &nodeTitle, double amount,
    const std::string &idempotencyKey)
{
    if (idempotencyKey.empty())
        return {false, false,
                {Application::ErrorCode::Validation, "idempotency key required", {}, false}};

    QSqlQuery lookup(m_database);
    lookup.prepare(QStringLiteral(
        "SELECT u.id, g.id, n.id, n.title FROM user_profiles_v3 u, goals_v3 g, "
        "content_nodes_v3 n WHERE u.uid=? AND g.uid=? AND n.uid=?"));
    lookup.addBindValue(QString::fromStdString(userId.value()));
    lookup.addBindValue(QString::fromStdString(goalId.value()));
    lookup.addBindValue(QString::fromStdString(nodeUid.value()));
    if (!lookup.exec() || !lookup.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user/goal/node not found", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    const std::string note = nodeTitle;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO progress_events_v4(uid, user_id, mel_id, goal_id, task_id, event_type, "
        "amount, unit, note, evidence_asset_uid, occurred_at, recorded_at, actor_type, "
        "idempotency_key) VALUES(?,?,NULL,?,NULL,'incremented',?,'ratio',?,NULL,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(eventUid.value()));
    query.addBindValue(lookup.value(0).toLongLong());
    query.addBindValue(lookup.value(1).toLongLong());
    query.addBindValue(amount);
    query.addBindValue(QString::fromStdString(note));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QStringLiteral("user"));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    if (!query.exec())
        return writeFailure("content progress event insert failed", query);
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
