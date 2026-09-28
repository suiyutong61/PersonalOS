#include "application/usecases/knowledge/ContentIngestUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

ContentIngestUseCases::ContentIngestUseCases(ContentIngestRepositoryPort &repo,
                                             ContentSourcePort &source, UuidPort &uids,
                                             const Domain::Clock &clock)
    : m_repo(repo), m_source(source), m_uids(uids), m_clock(clock)
{}

Result<Domain::ContentImportJob, ApplicationError> ContentIngestUseCases::submit(
    const SubmitInput &input)
{
    if (input.idempotencyKey.empty())
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::Validation, "idempotency key required", {}, false});
    if (m_repo.existsIdempotencyKey(input.idempotencyKey))
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::Conflict, "duplicate import idempotency key", {}, false});

    Domain::ContentImportJob job;
    job.uid = m_uids.next();
    job.sourceType = input.sourceType;
    job.sourceUri = input.sourceUri;
    job.status = Domain::IngestStatus::Queued;
    job.stage = "queued";
    job.idempotencyKey = input.idempotencyKey;
    job.requestedBy = input.requestedBy;
    const auto saved = m_repo.insert(job);
    if (!saved.ok)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(saved.error);
    return Result<Domain::ContentImportJob, ApplicationError>::success(std::move(job));
}

Result<Domain::ContentImportJob, ApplicationError> ContentIngestUseCases::advance(
    const Domain::Uid &jobUid, int expectedRevision)
{
    const auto current = m_repo.findByUid(jobUid);
    if (!current)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::NotFound, "import job not found", {}, false});

    Domain::ContentImportJob updated = *current;
    bool doFetch = false;

    if (current->status == Domain::IngestStatus::Queued
        || current->status == Domain::IngestStatus::FailedRetryable) {
        // 进入运行：管道起点 fetching（获取失败可由 fail() 落地重试）
        updated.status = Domain::IngestStatus::Running;
        updated.stage = Domain::IngestStage::Fetching;
        updated.errorCode.reset();
        updated.errorDetail.reset();
        updated.startedAt = m_clock.utcIso();
        doFetch = true;
    } else if (current->status == Domain::IngestStatus::Running) {
        if (current->stage == Domain::IngestStage::Validating) {
            // 管道末尾 → 等待用户确认
            updated.status = Domain::IngestStatus::AwaitingConfirmation;
            updated.stage = Domain::IngestStage::AwaitingConfirmation;
            updated.completedAt = m_clock.utcIso();
        } else {
            const char *next = Domain::IngestStage::nextStage(current->stage);
            if (!next)
                return Result<Domain::ContentImportJob, ApplicationError>::failure(
                    {ErrorCode::Conflict, "no next ingest stage", {}, false});
            updated.stage = next;
        }
    } else {
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::Conflict, "job is not advanceable", {}, false});
    }

    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;

    // fetching 阶段实际执行内容获取；获取失败由调用方以 fail() 落地
    if (doFetch) {
        m_lastFetched = m_source.fetch(current->sourceUri);
        if (!m_lastFetched.ok)
            return Result<Domain::ContentImportJob, ApplicationError>::failure(
                {ErrorCode::ExternalUnavailable, m_lastFetched.errorMessage, {}, true});
    }
    return Result<Domain::ContentImportJob, ApplicationError>::success(std::move(updated));
}

Result<Domain::ContentImportJob, ApplicationError> ContentIngestUseCases::fail(
    const Domain::Uid &jobUid, int expectedRevision, bool terminal, const std::string &code,
    const std::string &detail)
{
    const auto current = m_repo.findByUid(jobUid);
    if (!current)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::NotFound, "import job not found", {}, false});
    Domain::ContentImportJob updated = *current;
    updated.status =
        terminal ? Domain::IngestStatus::FailedTerminal : Domain::IngestStatus::FailedRetryable;
    updated.stage = current->stage;   // 管道阶段保留（重试从原阶段继续）
    updated.errorCode = code;
    updated.errorDetail = detail;
    updated.completedAt = terminal ? m_clock.utcIso() : std::optional<std::string>();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::ContentImportJob, ApplicationError>::success(std::move(updated));
}

Result<Domain::ContentImportJob, ApplicationError> ContentIngestUseCases::commit(
    const Domain::Uid &jobUid, int expectedRevision)
{
    const auto current = m_repo.findByUid(jobUid);
    if (!current)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::NotFound, "import job not found", {}, false});
    if (current->status != Domain::IngestStatus::AwaitingConfirmation)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(
            {ErrorCode::Conflict, "job is not awaiting confirmation", {}, false});
    Domain::ContentImportJob updated = *current;
    updated.status = Domain::IngestStatus::Committed;
    updated.stage = "committed";
    updated.completedAt = m_clock.utcIso();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ContentImportJob, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "ingest.committed", "ingest_job", updated.uid.value(),
                   "{\"stage\":\"committed\"}"});
    return Result<Domain::ContentImportJob, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
