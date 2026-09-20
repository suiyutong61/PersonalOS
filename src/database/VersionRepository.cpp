#include "database/VersionRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns =
    QStringLiteral("id, version_number, parent_version, description, status, created_at");

Version versionFromQuery(const QSqlQuery &q)
{
    Version v;
    v.id = q.value(QStringLiteral("id")).toLongLong();
    v.versionNumber = q.value(QStringLiteral("version_number")).toString();
    v.parentVersion = q.value(QStringLiteral("parent_version")).toString();
    v.description = q.value(QStringLiteral("description")).toString();
    v.status = q.value(QStringLiteral("status")).toString();
    v.createdAt = q.value(QStringLiteral("created_at")).toString();
    return v;
}

} // namespace

void VersionRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 VersionRepository::create(const Version &v)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO versions(version_number, parent_version, description, status) "
            "VALUES (?,?,?,?)"),
        {v.versionNumber, RepoUtil::nullableText(v.parentVersion),
         RepoUtil::nullableText(v.description), v.status});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::optional<Version> VersionRepository::getLatest() const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM versions ORDER BY id DESC LIMIT 1").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getLatest"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return versionFromQuery(q);
}

std::vector<Version> VersionRepository::getAll() const
{
    std::vector<Version> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM versions ORDER BY id DESC").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getAll"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(versionFromQuery(q));
    return out;
}

} // namespace PersonOS
