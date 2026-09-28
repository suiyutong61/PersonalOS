#include "infrastructure/persistence/SqlGoalRepository.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

// 读路径把 INTEGER 外键 JOIN 回 uid，领域层只见 Uid
const char *kColumns =
    "g.id, g.uid, u.uid AS user_uid, p.uid AS parent_uid, m.uid AS manifest_uid, "
    "g.title, g.description, g.goal_type, g.status, g.priority, g.target_at, "
    "g.desired_level_json, g.user_defined_level, g.sort_order, g.created_at, "
    "g.updated_at, g.revision";

const char *kFrom =
    " FROM goals_v3 g "
    "LEFT JOIN user_profiles_v3 u ON u.id = g.user_id "
    "LEFT JOIN goals_v3 p ON p.id = g.parent_goal_id "
    "LEFT JOIN domain_manifests_v3 m ON m.id = g.domain_manifest_id ";

} // namespace

SqlGoalRepository::SqlGoalRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

std::optional<Domain::Goal> SqlGoalRepository::fromQuery(QSqlQuery &query) const
{
    Domain::Goal goal;
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto userId = Domain::Uid::parse(query.value("user_uid").toString().toStdString());
    const auto manifest = Domain::Uid::parse(
        query.value("manifest_uid").toString().toStdString());
    if (!uid || !userId || !manifest)
        return std::nullopt;
    goal.uid = *uid;
    goal.userId = *userId;
    goal.domainManifestId = *manifest;
    if (!query.value("parent_uid").isNull()) {
        if (const auto parent = Domain::Uid::parse(
                query.value("parent_uid").toString().toStdString()))
            goal.parentGoalId = *parent;
    }
    goal.title = query.value("title").toString().toStdString();
    goal.description = query.value("description").toString().toStdString();
    goal.goalType = query.value("goal_type").toString().toStdString();
    if (const auto status = Domain::goalStatusFrom(
            query.value("status").toString().toStdString()))
        goal.status = *status;
    goal.priority = query.value("priority").toInt();
    if (!query.value("target_at").isNull())
        goal.targetAt = query.value("target_at").toString().toStdString();
    goal.desiredLevelJson = query.value("desired_level_json").toString().toStdString();
    goal.userDefinedLevel = query.value("user_defined_level").toInt() != 0;
    goal.sortOrder = query.value("sort_order").toInt();
    goal.createdAt = query.value("created_at").toString().toStdString();
    goal.updatedAt = query.value("updated_at").toString().toStdString();
    goal.revision = query.value("revision").toInt();
    return goal;
}

std::optional<Domain::Goal> SqlGoalRepository::findByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT %1%2 WHERE g.uid=?").arg(kColumns, kFrom));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return fromQuery(query);
}

std::vector<Domain::Goal> SqlGoalRepository::findByUser(const Domain::Uid &userId)
{
    std::vector<Domain::Goal> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1%2 WHERE g.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "ORDER BY g.sort_order, g.id")
                      .arg(kColumns, kFrom));
    query.addBindValue(QString::fromStdString(userId.value()));
    if (!query.exec())
        return out;
    while (query.next())
        if (const auto goal = fromQuery(query))
            out.push_back(*goal);
    return out;
}

std::vector<Domain::Goal> SqlGoalRepository::findChildren(const Domain::Uid &parentGoalId)
{
    std::vector<Domain::Goal> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1%2 WHERE g.parent_goal_id=(SELECT id FROM goals_v3 WHERE uid=?) "
        "ORDER BY g.sort_order, g.id")
                      .arg(kColumns, kFrom));
    query.addBindValue(QString::fromStdString(parentGoalId.value()));
    if (!query.exec())
        return out;
    while (query.next())
        if (const auto goal = fromQuery(query))
            out.push_back(*goal);
    return out;
}

Application::SaveResult SqlGoalRepository::writeFailure(const char *operation,
                                                         const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

Application::SaveResult SqlGoalRepository::insert(const Domain::Goal &goal)
{
    if (!goal.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "goal invalid", {}, false}};

    // JSON 字段入库前验证（数据库亦有 json_valid CHECK，此处给出更友好错误）
    if (!goal.desiredLevelJson.empty()
        && QJsonDocument::fromJson(
               QByteArray::fromStdString(goal.desiredLevelJson)).isNull())
        return {false, false,
                {Application::ErrorCode::Validation,
                 "desired_level_json is not valid JSON", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    // 外键列为 INTEGER id：通过 uid 子查询解析（领域层只使用 Uid）
    query.prepare(QStringLiteral(
        "INSERT INTO goals_v3(uid, user_id, parent_goal_id, domain_manifest_id, title, "
        "description, goal_type, status, priority, target_at, desired_level_json, "
        "user_defined_level, sort_order, created_at, updated_at, revision) "
        "VALUES(?, "
        "(SELECT id FROM user_profiles_v3 WHERE uid=?), "
        "(SELECT id FROM goals_v3 WHERE uid=?), "
        "(SELECT id FROM domain_manifests_v3 WHERE uid=?), "
        "?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(goal.uid.value()));
    query.addBindValue(QString::fromStdString(goal.userId.value()));
    query.addBindValue(goal.parentGoalId
                           ? QVariant(QString::fromStdString(goal.parentGoalId->value()))
                           : QVariant()); // NULL → 子查询无结果 → 列保持 NULL
    query.addBindValue(QString::fromStdString(goal.domainManifestId.value()));
    query.addBindValue(QString::fromStdString(goal.title));
    query.addBindValue(QString::fromStdString(goal.description));
    query.addBindValue(QString::fromStdString(goal.goalType));
    query.addBindValue(QString::fromStdString(Domain::toString(goal.status)));
    query.addBindValue(goal.priority);
    query.addBindValue(goal.targetAt
                           ? QVariant(QString::fromStdString(*goal.targetAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(goal.desiredLevelJson));
    query.addBindValue(goal.userDefinedLevel ? 1 : 0);
    query.addBindValue(goal.sortOrder);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("goal insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlGoalRepository::update(const Domain::Goal &goal, int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE goals_v3 SET title=?, description=?, goal_type=?, status=?, priority=?, "
        "target_at=?, desired_level_json=?, user_defined_level=?, sort_order=?, "
        "parent_goal_id=(SELECT id FROM goals_v3 WHERE uid=?), updated_at=?, "
        "revision=revision+1 WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(goal.title));
    query.addBindValue(QString::fromStdString(goal.description));
    query.addBindValue(QString::fromStdString(goal.goalType));
    query.addBindValue(QString::fromStdString(Domain::toString(goal.status)));
    query.addBindValue(goal.priority);
    query.addBindValue(goal.targetAt
                           ? QVariant(QString::fromStdString(*goal.targetAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(goal.desiredLevelJson));
    query.addBindValue(goal.userDefinedLevel ? 1 : 0);
    query.addBindValue(goal.sortOrder);
    query.addBindValue(goal.parentGoalId
                           ? QVariant(QString::fromStdString(goal.parentGoalId->value()))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(goal.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("goal update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "goal revision conflict", {}, false}};
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
