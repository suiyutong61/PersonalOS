#include "database/EventRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns =
    QStringLiteral("id, date, occurred_at, type, title, description, task_id, created_at");

Event eventFromQuery(const QSqlQuery &q)
{
    Event e;
    e.id = q.value(QStringLiteral("id")).toLongLong();
    e.date = q.value(QStringLiteral("date")).toString();
    e.occurredAt = q.value(QStringLiteral("occurred_at")).toString();
    e.type = q.value(QStringLiteral("type")).toString();
    e.title = q.value(QStringLiteral("title")).toString();
    e.description = q.value(QStringLiteral("description")).toString();
    e.taskId = q.value(QStringLiteral("task_id")).toLongLong(); // NULL → 0
    e.createdAt = q.value(QStringLiteral("created_at")).toString();
    return e;
}

} // namespace

void EventRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 EventRepository::append(const Event &e)
{
    // occurred_at 为空时绑定 NULL → 数据库 DEFAULT datetime('now','localtime') 生效
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO events(date, occurred_at, type, title, description, task_id) "
            "VALUES (?,?,?,?,?,?)"),
        {e.date, RepoUtil::nullableText(e.occurredAt), e.type, e.title,
         RepoUtil::nullableText(e.description), RepoUtil::nullableId(e.taskId)});
    if (!q.exec()) {
        fail(QStringLiteral("append"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::vector<Event> EventRepository::getByDate(const QString &date) const
{
    std::vector<Event> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM events WHERE date=? ORDER BY occurred_at, id").arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("getByDate"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(eventFromQuery(q));
    return out;
}

std::vector<Event> EventRepository::getRange(const QString &from, const QString &to) const
{
    std::vector<Event> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM events WHERE date BETWEEN ? AND ? ORDER BY date, occurred_at, id")
            .arg(kColumns),
        {from, to});
    if (!q.exec()) {
        fail(QStringLiteral("getRange"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(eventFromQuery(q));
    return out;
}

} // namespace PersonOS
