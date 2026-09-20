#include "database/TaskRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, plan_id, goal_id, title, description, planned_minutes, due_date, status, "
    "actual_minutes, sort_order, created_at, updated_at, completed_at");

Task taskFromQuery(const QSqlQuery &q)
{
    Task t;
    t.id = q.value(QStringLiteral("id")).toLongLong();
    t.planId = q.value(QStringLiteral("plan_id")).toLongLong();   // NULL → 0
    t.goalId = q.value(QStringLiteral("goal_id")).toLongLong();   // NULL → 0
    t.title = q.value(QStringLiteral("title")).toString();
    t.description = q.value(QStringLiteral("description")).toString();
    const QVariant planned = q.value(QStringLiteral("planned_minutes"));
    if (!planned.isNull())
        t.plannedMinutes = planned.toInt();
    t.dueDate = q.value(QStringLiteral("due_date")).toString();
    t.status = q.value(QStringLiteral("status")).toString();
    const QVariant actual = q.value(QStringLiteral("actual_minutes"));
    if (!actual.isNull())
        t.actualMinutes = actual.toInt();
    t.sortOrder = q.value(QStringLiteral("sort_order")).toInt();
    t.createdAt = q.value(QStringLiteral("created_at")).toString();
    t.updatedAt = q.value(QStringLiteral("updated_at")).toString();
    t.completedAt = q.value(QStringLiteral("completed_at")).toString();
    return t;
}

} // namespace

void TaskRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<Task> TaskRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM tasks WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return taskFromQuery(q);
}

std::vector<Task> TaskRepository::getByDate(const QString &date) const
{
    std::vector<Task> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM tasks WHERE due_date=? ORDER BY sort_order, id").arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("getByDate"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(taskFromQuery(q));
    return out;
}

std::vector<Task> TaskRepository::getByPlan(qint64 planId) const
{
    std::vector<Task> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM tasks WHERE plan_id=? ORDER BY sort_order, id").arg(kColumns),
        {planId});
    if (!q.exec()) {
        fail(QStringLiteral("getByPlan"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(taskFromQuery(q));
    return out;
}

qint64 TaskRepository::create(const Task &t)
{
    // createdAt/updatedAt 由数据库 DEFAULT 生成
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO tasks(plan_id, goal_id, title, description, planned_minutes, due_date, "
            "status, actual_minutes, sort_order) VALUES (?,?,?,?,?,?,?,?,?)"),
        {RepoUtil::nullableId(t.planId), RepoUtil::nullableId(t.goalId), t.title,
         RepoUtil::nullableText(t.description), RepoUtil::nullableInt(t.plannedMinutes), t.dueDate,
         t.status, RepoUtil::nullableInt(t.actualMinutes), t.sortOrder});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool TaskRepository::update(const Task &t)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "UPDATE tasks SET plan_id=?, goal_id=?, title=?, description=?, planned_minutes=?, "
            "due_date=?, status=?, actual_minutes=?, sort_order=?, completed_at=?, "
            "updated_at=datetime('now','localtime') WHERE id=?"),
        {RepoUtil::nullableId(t.planId), RepoUtil::nullableId(t.goalId), t.title,
         RepoUtil::nullableText(t.description), RepoUtil::nullableInt(t.plannedMinutes), t.dueDate,
         t.status, RepoUtil::nullableInt(t.actualMinutes), t.sortOrder,
         RepoUtil::nullableText(t.completedAt), t.id});
    if (!q.exec()) {
        fail(QStringLiteral("update"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
