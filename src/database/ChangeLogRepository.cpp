#include "database/ChangeLogRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, target_type, target_id, before_summary, after_summary, reason, "
    "proposal_id, version_id, created_at");

ChangeLog changeLogFromQuery(const QSqlQuery &q)
{
    ChangeLog c;
    c.id = q.value(QStringLiteral("id")).toLongLong();
    c.targetType = q.value(QStringLiteral("target_type")).toString();
    c.targetId = q.value(QStringLiteral("target_id")).toLongLong();
    c.beforeSummary = q.value(QStringLiteral("before_summary")).toString();
    c.afterSummary = q.value(QStringLiteral("after_summary")).toString();
    c.reason = q.value(QStringLiteral("reason")).toString();
    c.proposalId = q.value(QStringLiteral("proposal_id")).toLongLong(); // NULL → 0
    c.versionId = q.value(QStringLiteral("version_id")).toLongLong();   // NULL → 0
    c.createdAt = q.value(QStringLiteral("created_at")).toString();
    return c;
}

} // namespace

void ChangeLogRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 ChangeLogRepository::append(const ChangeLog &c)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO change_logs(target_type, target_id, before_summary, after_summary, "
            "reason, proposal_id, version_id) VALUES (?,?,?,?,?,?,?)"),
        {c.targetType, c.targetId, RepoUtil::nullableText(c.beforeSummary),
         RepoUtil::nullableText(c.afterSummary), c.reason, RepoUtil::nullableId(c.proposalId),
         RepoUtil::nullableId(c.versionId)});
    if (!q.exec()) {
        fail(QStringLiteral("append"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::vector<ChangeLog> ChangeLogRepository::getByTarget(const QString &targetType,
                                                        qint64 targetId) const
{
    std::vector<ChangeLog> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM change_logs WHERE target_type=? AND target_id=? "
                       "ORDER BY created_at, id")
            .arg(kColumns),
        {targetType, targetId});
    if (!q.exec()) {
        fail(QStringLiteral("getByTarget"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(changeLogFromQuery(q));
    return out;
}

} // namespace PersonOS
