#include "database/GoalRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, parent_id, level, title, description, target_date, status, priority, created_at, updated_at");

Goal goalFromQuery(const QSqlQuery &q)
{
    Goal g;
    g.id = q.value(QStringLiteral("id")).toLongLong();
    g.parentId = q.value(QStringLiteral("parent_id")).toLongLong(); // NULL → 0
    g.level = q.value(QStringLiteral("level")).toString();
    g.title = q.value(QStringLiteral("title")).toString();
    g.description = q.value(QStringLiteral("description")).toString();
    g.targetDate = q.value(QStringLiteral("target_date")).toString();
    g.status = q.value(QStringLiteral("status")).toString();
    g.priority = q.value(QStringLiteral("priority")).toInt();
    g.createdAt = q.value(QStringLiteral("created_at")).toString();
    g.updatedAt = q.value(QStringLiteral("updated_at")).toString();
    return g;
}

} // namespace

void GoalRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<Goal> GoalRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(QStringLiteral("SELECT %1 FROM goals WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return goalFromQuery(q);
}

std::vector<Goal> GoalRepository::getAll() const
{
    std::vector<Goal> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM goals ORDER BY priority DESC, id").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getAll"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(goalFromQuery(q));
    return out;
}

std::vector<Goal> GoalRepository::getChildren(qint64 parentId) const
{
    std::vector<Goal> out;
    QSqlQuery q;
    if (parentId == 0) {
        q = RepoUtil::query(QStringLiteral(
            "SELECT %1 FROM goals WHERE parent_id IS NULL ORDER BY priority DESC, id")
                                .arg(kColumns));
    } else {
        q = RepoUtil::query(QStringLiteral(
                                "SELECT %1 FROM goals WHERE parent_id=? ORDER BY priority DESC, id")
                                .arg(kColumns),
                            {parentId});
    }
    if (!q.exec()) {
        fail(QStringLiteral("getChildren"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(goalFromQuery(q));
    return out;
}

std::vector<Goal> GoalRepository::getByLevel(const QString &level) const
{
    std::vector<Goal> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM goals WHERE level=? ORDER BY priority DESC, id").arg(kColumns),
        {level});
    if (!q.exec()) {
        fail(QStringLiteral("getByLevel"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(goalFromQuery(q));
    return out;
}

qint64 GoalRepository::create(const Goal &g)
{
    // createdAt/updatedAt 由数据库 DEFAULT 生成（Migrations.h schema v1）
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO goals(parent_id, level, title, description, target_date, status, priority) "
            "VALUES (?,?,?,?,?,?,?)"),
        {RepoUtil::nullableId(g.parentId), g.level, g.title, RepoUtil::nullableText(g.description),
         RepoUtil::nullableText(g.targetDate), g.status, g.priority});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool GoalRepository::update(const Goal &g)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "UPDATE goals SET parent_id=?, level=?, title=?, description=?, target_date=?, "
            "status=?, priority=?, updated_at=datetime('now','localtime') WHERE id=?"),
        {RepoUtil::nullableId(g.parentId), g.level, g.title, RepoUtil::nullableText(g.description),
         RepoUtil::nullableText(g.targetDate), g.status, g.priority, g.id});
    if (!q.exec()) {
        fail(QStringLiteral("update"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
