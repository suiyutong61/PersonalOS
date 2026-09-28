#include "infrastructure/persistence/SqlReviewRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

const char *kColumns =
    "uid, status, started_at, completed_at, summary, user_comment, next_action, "
    "knowledge_snapshot_uid, revision";

std::optional<Domain::Review> reviewFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::Review review;
    review.uid = *uid;
    if (const auto status =
            Domain::reviewStatusFrom(query.value("status").toString().toStdString()))
        review.status = *status;
    review.startedAt = query.value("started_at").toString().toStdString();
    if (!query.value("completed_at").isNull())
        review.completedAt = query.value("completed_at").toString().toStdString();
    review.summary = query.value("summary").toString().toStdString();
    review.userComment = query.value("user_comment").toString().toStdString();
    review.nextAction = query.value("next_action").toString().toStdString();
    if (!query.value("knowledge_snapshot_uid").isNull())
        review.knowledgeSnapshotUid =
            query.value("knowledge_snapshot_uid").toString().toStdString();
    review.revision = query.value("revision").toInt();
    return review;
}

} // namespace

SqlReviewRepository::SqlReviewRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlReviewRepository::writeFailure(const char *operation,
                                                           const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<Domain::Review> SqlReviewRepository::findByMel(const Domain::Uid &melId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM reviews_v4 WHERE mel_id=(SELECT id FROM mels_v4 WHERE uid=?)")
                      .arg(kColumns));
    query.addBindValue(QString::fromStdString(melId.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto review = reviewFromQuery(query);
    if (review)
        review->melId = melId;
    return review;
}

std::optional<Domain::Review> SqlReviewRepository::findByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    // 列名全部限定到 r：mels_v4 也有 knowledge_snapshot_uid，未限定会歧义
    query.prepare(QStringLiteral(
        "SELECT r.uid, r.status, r.started_at, r.completed_at, r.summary, r.user_comment, "
        "r.next_action, r.knowledge_snapshot_uid, r.revision, m.uid AS mel_uid "
        "FROM reviews_v4 r JOIN mels_v4 m ON m.id=r.mel_id WHERE r.uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto review = reviewFromQuery(query);
    if (!review)
        return std::nullopt;
    if (const auto melUid =
            Domain::Uid::parse(query.value("mel_uid").toString().toStdString()))
        review->melId = *melUid;
    return review;
}

std::vector<Domain::Review> SqlReviewRepository::listRecent(int limit)
{
    std::vector<Domain::Review> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT r.uid, r.status, r.started_at, r.completed_at, r.summary, r.user_comment, "
        "r.next_action, r.knowledge_snapshot_uid, r.revision, m.uid AS mel_uid "
        "FROM reviews_v4 r JOIN mels_v4 m ON m.id=r.mel_id ORDER BY r.id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        if (const auto review = reviewFromQuery(query)) {
            auto value = *review;
            if (const auto melUid =
                    Domain::Uid::parse(query.value("mel_uid").toString().toStdString()))
                value.melId = *melUid;
            out.push_back(std::move(value));
        }
    }
    return out;
}

Application::SaveResult SqlReviewRepository::insert(const Domain::Review &review)
{
    if (!review.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "review invalid", {}, false}};
    QSqlQuery melQuery(m_database);
    melQuery.prepare(QStringLiteral("SELECT id FROM mels_v4 WHERE uid=?"));
    melQuery.addBindValue(QString::fromStdString(review.melId.value()));
    if (!melQuery.exec() || !melQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "mel not found", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO reviews_v4(uid, mel_id, status, started_at, completed_at, summary, "
        "user_comment, next_action, knowledge_snapshot_uid, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(review.uid.value()));
    query.addBindValue(melQuery.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(Domain::toString(review.status)));
    query.addBindValue(QString::fromStdString(review.startedAt));
    query.addBindValue(review.completedAt
                           ? QVariant(QString::fromStdString(*review.completedAt))
                           : QVariant());
    query.addBindValue(review.summary.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.summary)));
    query.addBindValue(review.userComment.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.userComment)));
    query.addBindValue(review.nextAction.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.nextAction)));
    query.addBindValue(review.knowledgeSnapshotUid
                           ? QVariant(QString::fromStdString(*review.knowledgeSnapshotUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("review insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlReviewRepository::update(const Domain::Review &review,
                                                    int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE reviews_v4 SET status=?, completed_at=?, summary=?, user_comment=?, "
        "next_action=?, knowledge_snapshot_uid=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(review.status)));
    query.addBindValue(review.completedAt
                           ? QVariant(QString::fromStdString(*review.completedAt))
                           : QVariant());
    query.addBindValue(review.summary.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.summary)));
    query.addBindValue(review.userComment.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.userComment)));
    query.addBindValue(review.nextAction.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(review.nextAction)));
    query.addBindValue(review.knowledgeSnapshotUid
                           ? QVariant(QString::fromStdString(*review.knowledgeSnapshotUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(review.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("review update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "review revision conflict", {}, false}};
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
