#include "application/usecases/review/ReviewUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

ReviewUseCases::ReviewUseCases(ReviewRepository &repo, UuidPort &uids,
                               const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<Domain::Review, ApplicationError> ReviewUseCases::openReview(const Domain::Uid &melUid)
{
    if (m_repo.findByMel(melUid))
        return Result<Domain::Review, ApplicationError>::failure(
            {ErrorCode::Conflict, "review already exists for mel", {}, false});

    Domain::Review review;
    review.uid = m_uids.next();
    review.melId = melUid;
    review.status = Domain::ReviewStatus::Collecting;
    review.startedAt = m_clock.utcIso();
    const auto saved = m_repo.insert(review);
    if (!saved.ok)
        return Result<Domain::Review, ApplicationError>::failure(saved.error);
    Audit::record({"system", {}, "review.opened", "review", review.uid.value(), "{}"});
    return Result<Domain::Review, ApplicationError>::success(std::move(review));
}

Result<Domain::Review, ApplicationError> ReviewUseCases::submitReview(
    const Domain::Uid &melUid, int expectedRevision, const SubmitInput &input)
{
    const auto current = m_repo.findByMel(melUid);
    if (!current)
        return Result<Domain::Review, ApplicationError>::failure(
            {ErrorCode::NotFound, "review not found", {}, false});
    if (current->status == Domain::ReviewStatus::Closed)
        return Result<Domain::Review, ApplicationError>::failure(
            {ErrorCode::Conflict, "review already closed", {}, false});

    Domain::Review updated = *current;
    updated.status = Domain::ReviewStatus::Confirmed;
    updated.summary = input.summary;
    updated.userComment = input.userComment;
    updated.nextAction = input.nextAction;
    updated.knowledgeSnapshotUid = input.knowledgeSnapshotUid;
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Review, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "review.updated", "review", updated.uid.value(),
                   "{\"status\":\"" + Domain::toString(updated.status) + "\"}"});
    return Result<Domain::Review, ApplicationError>::success(std::move(updated));
}

Result<Domain::Review, ApplicationError> ReviewUseCases::closeReview(
    const Domain::Uid &melUid, int expectedRevision)
{
    const auto current = m_repo.findByMel(melUid);
    if (!current)
        return Result<Domain::Review, ApplicationError>::failure(
            {ErrorCode::NotFound, "review not found", {}, false});
    Domain::Review updated = *current;
    updated.status = Domain::ReviewStatus::Closed;
    updated.completedAt = m_clock.utcIso();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Review, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::Review, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
