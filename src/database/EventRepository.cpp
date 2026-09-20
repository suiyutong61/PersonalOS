#include "database/EventRepository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

#include "database/ChangeLogRepository.h"
#include "database/RepoUtil.h"
#include "models/ChangeLog.h"

namespace PersonOS {

namespace {

const QString kColumns =
    QStringLiteral("id, date, occurred_at, type, title, description, task_id, created_at");

QString summaryOf(const Event &e)
{
    return QStringLiteral("date=%1 type=%2 title=%3 desc=%4")
        .arg(e.date, e.type, e.title, e.description);
}

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

std::optional<Event> EventRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM events WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return eventFromQuery(q);
}

qint64 EventRepository::append(const Event &e)
{
    // 注意：occurred_at 是 NOT NULL 列，SQLite 的 DEFAULT 只在列被"省略"时生效，
    // 显式绑定 NULL 会触发 NOT NULL 约束错误（README 3.10 踩坑）。此处由客户端填充。
    Event event = e;
    if (event.occurredAt.isEmpty())
        event.occurredAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO events(date, occurred_at, type, title, description, task_id) "
            "VALUES (?,?,?,?,?,?)"),
        {event.date, event.occurredAt, event.type, event.title,
         RepoUtil::nullableText(event.description), RepoUtil::nullableId(event.taskId)});
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

bool EventRepository::correct(qint64 id, const Event &fix, const QString &reason)
{
    // 2.4.14 原则1：原始记录 → 修正 → 修正原因；先留痕，再更新。
    const auto original = getById(id);
    if (!original) {
        fail(QStringLiteral("correct"), QStringLiteral("事件不存在（id=%1）").arg(id));
        return false;
    }
    if (reason.trimmed().isEmpty()) {
        fail(QStringLiteral("correct"), QStringLiteral("修正必须提供原因"));
        return false;
    }

    Event fixed = *original;
    fixed.type = fix.type;
    fixed.title = fix.title;
    fixed.description = fix.description; // 仅允许修正 type/title/description

    ChangeLog log;
    log.targetType = QStringLiteral("event");
    log.targetId = id;
    log.beforeSummary = summaryOf(*original);
    log.afterSummary = summaryOf(fixed);
    log.reason = reason;

    ChangeLogRepository logs;
    if (logs.append(log) == 0) {
        fail(QStringLiteral("correct"), logs.lastError());
        return false;
    }

    auto q = RepoUtil::query(
        QStringLiteral("UPDATE events SET type=?, title=?, description=? WHERE id=?"),
        {fixed.type, fixed.title, RepoUtil::nullableText(fixed.description), id});
    if (!q.exec()) {
        fail(QStringLiteral("correct"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
