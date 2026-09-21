#include "database/ReviewRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, review_type, period_start, period_end, summary, problems, causes, "
    "next_actions, created_at");

Review reviewFromQuery(const QSqlQuery &q)
{
    Review r;
    r.id = q.value(QStringLiteral("id")).toLongLong();
    r.reviewType = q.value(QStringLiteral("review_type")).toString();
    r.periodStart = q.value(QStringLiteral("period_start")).toString();
    r.periodEnd = q.value(QStringLiteral("period_end")).toString();
    r.summary = q.value(QStringLiteral("summary")).toString();
    r.problems = q.value(QStringLiteral("problems")).toString();
    r.causes = q.value(QStringLiteral("causes")).toString();
    r.nextActions = q.value(QStringLiteral("next_actions")).toString();
    r.createdAt = q.value(QStringLiteral("created_at")).toString();
    return r;
}

} // namespace

void ReviewRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<Review> ReviewRepository::getDaily(const QString &date) const
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "SELECT %1 FROM reviews WHERE review_type='daily' AND period_start=?").arg(kColumns),
        {date});
    if (!q.exec()) {
        fail(QStringLiteral("getDaily"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return reviewFromQuery(q);
}

bool ReviewRepository::upsert(const Review &r)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO reviews(review_type, period_start, period_end, summary, problems, "
            "causes, next_actions) VALUES (?,?,?,?,?,?,?) "
            "ON CONFLICT(review_type, period_start) DO UPDATE SET "
            "summary=excluded.summary, problems=excluded.problems, causes=excluded.causes, "
            "next_actions=excluded.next_actions"),
        {r.reviewType, r.periodStart, RepoUtil::nullableText(r.periodEnd), r.summary,
         RepoUtil::nullableText(r.problems), RepoUtil::nullableText(r.causes),
         RepoUtil::nullableText(r.nextActions)});
    if (!q.exec()) {
        fail(QStringLiteral("upsert"), q.lastError().text());
        return false;
    }
    return true;
}

} // namespace PersonOS
