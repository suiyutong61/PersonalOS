#include "database/ProposalRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, target_type, target_id, current_value, proposed_value, reason, evidence, "
    "expected_effect, risk, status, created_at, decided_at");

Proposal proposalFromQuery(const QSqlQuery &q)
{
    Proposal p;
    p.id = q.value(QStringLiteral("id")).toLongLong();
    p.targetType = q.value(QStringLiteral("target_type")).toString();
    p.targetId = q.value(QStringLiteral("target_id")).toLongLong(); // NULL → 0
    p.currentValue = q.value(QStringLiteral("current_value")).toString();
    p.proposedValue = q.value(QStringLiteral("proposed_value")).toString();
    p.reason = q.value(QStringLiteral("reason")).toString();
    p.evidence = q.value(QStringLiteral("evidence")).toString();
    p.expectedEffect = q.value(QStringLiteral("expected_effect")).toString();
    p.risk = q.value(QStringLiteral("risk")).toString();
    p.status = q.value(QStringLiteral("status")).toString();
    p.createdAt = q.value(QStringLiteral("created_at")).toString();
    p.decidedAt = q.value(QStringLiteral("decided_at")).toString();
    return p;
}

} // namespace

void ProposalRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 ProposalRepository::create(const Proposal &p)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO proposals(target_type, target_id, current_value, proposed_value, "
            "reason, evidence, expected_effect, risk, status) VALUES (?,?,?,?,?,?,?,?,?)"),
        {p.targetType, RepoUtil::nullableId(p.targetId), RepoUtil::nullableText(p.currentValue),
         p.proposedValue, p.reason, RepoUtil::nullableText(p.evidence),
         RepoUtil::nullableText(p.expectedEffect), RepoUtil::nullableText(p.risk), p.status});
    if (!q.exec()) {
        fail(QStringLiteral("create"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::optional<Proposal> ProposalRepository::getById(qint64 id) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM proposals WHERE id=?").arg(kColumns), {id});
    if (!q.exec()) {
        fail(QStringLiteral("getById"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return proposalFromQuery(q);
}

std::vector<Proposal> ProposalRepository::getByStatus(const QString &status) const
{
    std::vector<Proposal> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM proposals WHERE status=? ORDER BY id").arg(kColumns),
        {status});
    if (!q.exec()) {
        fail(QStringLiteral("getByStatus"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(proposalFromQuery(q));
    return out;
}

bool ProposalRepository::setStatus(qint64 id, const QString &status, const QString &decidedAt)
{
    auto q = RepoUtil::query(
        QStringLiteral("UPDATE proposals SET status=?, decided_at=? WHERE id=?"),
        {status, RepoUtil::nullableText(decidedAt), id});
    if (!q.exec()) {
        fail(QStringLiteral("setStatus"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

} // namespace PersonOS
