#include "database/CoreValueRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral("id, name, description, sort_order, created_at");

CoreValue coreValueFromQuery(const QSqlQuery &q)
{
    CoreValue v;
    v.id = q.value(QStringLiteral("id")).toLongLong();
    v.name = q.value(QStringLiteral("name")).toString();
    v.description = q.value(QStringLiteral("description")).toString();
    v.sortOrder = q.value(QStringLiteral("sort_order")).toInt();
    v.createdAt = q.value(QStringLiteral("created_at")).toString();
    return v;
}

} // namespace

void CoreValueRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::vector<CoreValue> CoreValueRepository::getAll() const
{
    std::vector<CoreValue> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM core_values ORDER BY sort_order, id").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getAll"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(coreValueFromQuery(q));
    return out;
}

std::optional<CoreValue> CoreValueRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM core_values WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return coreValueFromQuery(q);
}

qint64 CoreValueRepository::create(const CoreValue &v)
{
    auto q = RepoUtil::query(
        QStringLiteral("INSERT INTO core_values(name, description, sort_order) VALUES (?,?,?)"),
        {v.name, RepoUtil::nullableText(v.description), v.sortOrder});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool CoreValueRepository::update(const CoreValue &v)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "UPDATE core_values SET name=?, description=?, sort_order=? WHERE id=?"),
        {v.name, RepoUtil::nullableText(v.description), v.sortOrder, v.id});
    if (!q.exec()) {
        fail(QStringLiteral("update"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
