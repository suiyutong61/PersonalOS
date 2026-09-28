#include "database/MetricRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral("id, date, key, value, window, note, created_at");

Metric metricFromQuery(const QSqlQuery &q)
{
    Metric m;
    m.id = q.value(QStringLiteral("id")).toLongLong();
    m.date = q.value(QStringLiteral("date")).toString();
    m.key = q.value(QStringLiteral("key")).toString();
    m.value = q.value(QStringLiteral("value")).toDouble();
    m.window = q.value(QStringLiteral("window")).toString();
    m.note = q.value(QStringLiteral("note")).toString();
    m.createdAt = q.value(QStringLiteral("created_at")).toString();
    return m;
}

} // namespace

void MetricRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<Metric> MetricRepository::get(const QString &date, const QString &key,
                                            const QString &window) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM metrics WHERE date=? AND key=? AND window=?").arg(kColumns),
        {date, key, window});
    if (!q.exec()) {
        fail(QStringLiteral("get"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return metricFromQuery(q);
}

std::vector<Metric> MetricRepository::getByDate(const QString &date) const
{
    std::vector<Metric> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM metrics WHERE date=? ORDER BY key, window").arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("getByDate"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(metricFromQuery(q));
    return out;
}

bool MetricRepository::upsert(const Metric &m)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO metrics(date, key, value, window, note) VALUES (?,?,?,?,?) "
            "ON CONFLICT(date, key, window) DO UPDATE SET "
            "value=excluded.value, note=excluded.note"),
        {m.date, m.key, m.value, RepoUtil::nullableText(m.window), RepoUtil::nullableText(m.note)});
    if (!q.exec()) {
        fail(QStringLiteral("upsert"), q.lastError().text());
        return false;
    }
    return true;
}

} // namespace PersonOS
