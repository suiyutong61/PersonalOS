#include "database/PrincipleRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral("id, text, sort_order, created_at");

Principle principleFromQuery(const QSqlQuery &q)
{
    Principle p;
    p.id = q.value(QStringLiteral("id")).toLongLong();
    p.text = q.value(QStringLiteral("text")).toString();
    p.sortOrder = q.value(QStringLiteral("sort_order")).toInt();
    p.createdAt = q.value(QStringLiteral("created_at")).toString();
    return p;
}

} // namespace

void PrincipleRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::vector<Principle> PrincipleRepository::getAll() const
{
    std::vector<Principle> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM principles ORDER BY sort_order, id").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getAll"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(principleFromQuery(q));
    return out;
}

std::optional<Principle> PrincipleRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM principles WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return principleFromQuery(q);
}

qint64 PrincipleRepository::create(const Principle &p)
{
    auto q = RepoUtil::query(
        QStringLiteral("INSERT INTO principles(text, sort_order) VALUES (?,?)"),
        {p.text, p.sortOrder});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool PrincipleRepository::update(const Principle &p)
{
    auto q = RepoUtil::query(
        QStringLiteral("UPDATE principles SET text=?, sort_order=? WHERE id=?"),
        {p.text, p.sortOrder, p.id});
    if (!q.exec()) {
        fail(QStringLiteral("update"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
