#include "database/StateRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, date, sleep_hours, energy, focus, mood, note, created_at, updated_at");

StateSnapshot snapshotFromQuery(const QSqlQuery &q)
{
    StateSnapshot s;
    s.id = q.value(QStringLiteral("id")).toLongLong();
    s.date = q.value(QStringLiteral("date")).toString();
    const QVariant sleep = q.value(QStringLiteral("sleep_hours"));
    if (!sleep.isNull())
        s.sleepHours = sleep.toDouble();
    const QVariant energy = q.value(QStringLiteral("energy"));
    if (!energy.isNull())
        s.energy = energy.toInt();
    const QVariant focus = q.value(QStringLiteral("focus"));
    if (!focus.isNull())
        s.focus = focus.toInt();
    const QVariant mood = q.value(QStringLiteral("mood"));
    if (!mood.isNull())
        s.mood = mood.toInt();
    s.note = q.value(QStringLiteral("note")).toString();
    s.createdAt = q.value(QStringLiteral("created_at")).toString();
    s.updatedAt = q.value(QStringLiteral("updated_at")).toString();
    return s;
}

} // namespace

void StateRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<StateSnapshot> StateRepository::getByDate(const QString &date) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM state_snapshots WHERE date=?").arg(kColumns), {date});
    if (!q.exec()) {
        fail(QStringLiteral("getByDate"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return snapshotFromQuery(q);
}

bool StateRepository::upsert(const StateSnapshot &s)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO state_snapshots(date, sleep_hours, energy, focus, mood, note) "
            "VALUES (?,?,?,?,?,?) "
            "ON CONFLICT(date) DO UPDATE SET sleep_hours=excluded.sleep_hours, "
            "energy=excluded.energy, focus=excluded.focus, mood=excluded.mood, "
            "note=excluded.note, updated_at=datetime('now','localtime')"),
        {s.date, RepoUtil::nullableReal(s.sleepHours), RepoUtil::nullableInt(s.energy),
         RepoUtil::nullableInt(s.focus), RepoUtil::nullableInt(s.mood),
         RepoUtil::nullableText(s.note)});
    if (!q.exec()) {
        fail(QStringLiteral("upsert"), q.lastError().text());
        return false;
    }
    return true;
}

} // namespace PersonOS
