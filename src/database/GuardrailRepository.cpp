#include "database/GuardrailRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, date, type, rule, data_snapshot, threshold, suggestion, evidence_ref, "
    "status, handled_by, created_at");

GuardrailWarning warningFromQuery(const QSqlQuery &q)
{
    GuardrailWarning w;
    w.id = q.value(QStringLiteral("id")).toLongLong();
    w.date = q.value(QStringLiteral("date")).toString();
    w.type = q.value(QStringLiteral("type")).toString();
    w.rule = q.value(QStringLiteral("rule")).toString();
    w.dataSnapshot = q.value(QStringLiteral("data_snapshot")).toString();
    w.threshold = q.value(QStringLiteral("threshold")).toString();
    w.suggestion = q.value(QStringLiteral("suggestion")).toString();
    w.evidenceRef = q.value(QStringLiteral("evidence_ref")).toString();
    w.status = q.value(QStringLiteral("status")).toString();
    w.handledBy = q.value(QStringLiteral("handled_by")).toString();
    w.createdAt = q.value(QStringLiteral("created_at")).toString();
    return w;
}

} // namespace

void GuardrailRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 GuardrailRepository::append(const GuardrailWarning &w)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO guardrail_warnings(date, type, rule, data_snapshot, threshold, "
            "suggestion, evidence_ref, status) VALUES (?,?,?,?,?,?,?,?)"),
        {w.date, w.type, w.rule, w.dataSnapshot, w.threshold,
         RepoUtil::nullableText(w.suggestion), RepoUtil::nullableText(w.evidenceRef), w.status});
    if (!q.exec()) {
        fail(QStringLiteral("append"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::optional<GuardrailWarning> GuardrailRepository::activeForDate(const QString &type,
                                                                  const QString &date) const
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "SELECT %1 FROM guardrail_warnings WHERE type=? AND date=? AND status='active' "
            "ORDER BY id DESC LIMIT 1")
            .arg(kColumns),
        {type, date});
    if (!q.exec()) {
        fail(QStringLiteral("activeForDate"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return warningFromQuery(q);
}

std::vector<GuardrailWarning> GuardrailRepository::active(const QString &date) const
{
    std::vector<GuardrailWarning> out;
    auto q = RepoUtil::query(
        QStringLiteral(
            "SELECT %1 FROM guardrail_warnings WHERE date=? AND status='active' ORDER BY id")
            .arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("active"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(warningFromQuery(q));
    return out;
}

bool GuardrailRepository::setStatus(qint64 id, const QString &status, const QString &handledBy)
{
    auto q = RepoUtil::query(
        QStringLiteral("UPDATE guardrail_warnings SET status=?, handled_by=? WHERE id=?"),
        {status, RepoUtil::nullableText(handledBy), id});
    if (!q.exec()) {
        fail(QStringLiteral("setStatus"), q.lastError().text());
        return false;
    }
    return q.numRowsAffected() > 0;
}

int GuardrailRepository::countSince(const QString &type, const QString &from,
                                    const QString &to) const
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "SELECT COUNT(*) FROM guardrail_warnings WHERE type=? AND date BETWEEN ? AND ?"),
        {type, from, to});
    if (!q.exec()) {
        fail(QStringLiteral("countSince"), q.lastError().text());
        return 0;
    }
    if (!q.next())
        return 0;
    return q.value(0).toInt();
}

} // namespace PersonOS
