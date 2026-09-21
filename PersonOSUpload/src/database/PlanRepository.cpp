#include "database/PlanRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, period_type, period_start, period_end, status, note, created_at");

Plan planFromQuery(const QSqlQuery &q)
{
    Plan p;
    p.id = q.value(QStringLiteral("id")).toLongLong();
    p.periodType = q.value(QStringLiteral("period_type")).toString();
    p.periodStart = q.value(QStringLiteral("period_start")).toString();
    p.periodEnd = q.value(QStringLiteral("period_end")).toString();
    p.status = q.value(QStringLiteral("status")).toString();
    p.note = q.value(QStringLiteral("note")).toString();
    p.createdAt = q.value(QStringLiteral("created_at")).toString();
    return p;
}

} // namespace

void PlanRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<Plan> PlanRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM plans WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return planFromQuery(q);
}

std::optional<Plan> PlanRepository::getDaily(const QString &date) const
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "SELECT %1 FROM plans WHERE period_type='daily' AND period_start=?").arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("getDaily"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return planFromQuery(q);
}

qint64 PlanRepository::ensureDaily(const QString &date)
{
    // 快路径：已存在直接返回
    if (const auto existing = getDaily(date))
        return existing->id;

    // 插入；UNIQUE(period_type, period_start) 兜底并发冲突
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO plans(period_type, period_start) VALUES ('daily', ?) "
            "ON CONFLICT(period_type, period_start) DO NOTHING"),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("ensureDaily"), q.lastError().text());
        return 0;
    }
    if (const auto created = getDaily(date))
        return created->id;

    fail(QStringLiteral("ensureDaily"), QStringLiteral("创建后仍查不到计划"));
    return 0;
}

bool PlanRepository::setStatus(qint64 id, const QString &status)
{
    auto q = RepoUtil::query(
        QStringLiteral("UPDATE plans SET status=? WHERE id=?"), {status, id});
    if (!q.exec()) {
        fail(QStringLiteral("setStatus"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
