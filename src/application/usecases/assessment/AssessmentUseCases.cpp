#include "application/usecases/assessment/AssessmentUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

AssessmentUseCases::AssessmentUseCases(AssessmentRepository &repo, UuidPort &uids,
                                       const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<AssessmentUseCases::CreateOutput, ApplicationError> AssessmentUseCases::createAssessment(
    const CreateInput &input)
{
    if (input.items.empty())
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "assessment must contain at least one item", {}, false});

    Domain::Assessment assessment;
    assessment.uid = m_uids.next();
    assessment.userId = input.userId;
    assessment.goalId = input.goalId;
    assessment.melId = input.melId;
    assessment.assessmentType = input.assessmentType;
    assessment.status = Domain::AssessmentStatus::Draft;
    assessment.scopeJson = input.scopeJson;
    assessment.rubricJson = input.rubricJson;
    assessment.generatedBy = input.generatedBy;
    assessment.knowledgeSnapshotUid = input.knowledgeSnapshotUid;
    if (!assessment.isValid())
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "assessment invalid", {}, false});

    const auto saved = m_repo.insert(assessment);
    if (!saved.ok)
        return Result<CreateOutput, ApplicationError>::failure(saved.error);

    int sequence = 0;
    for (const auto &proto : input.items) {
        Domain::AssessmentItem item = proto;
        item.uid = m_uids.next();
        item.assessmentId = assessment.uid;
        item.sequenceNo = sequence++;
        const auto itemSaved = m_repo.insertItem(item);
        if (!itemSaved.ok)
            return Result<CreateOutput, ApplicationError>::failure(itemSaved.error);
    }
    Audit::record({"user", {}, "assessment.created", "assessment", assessment.uid.value(),
                   "{\"type\":\"" + assessment.assessmentType + "\"}"});
    return Result<CreateOutput, ApplicationError>::success(CreateOutput{std::move(assessment)});
}

Result<Domain::Assessment, ApplicationError> AssessmentUseCases::readyAssessment(
    const Domain::Uid &assessmentUid, int expectedRevision)
{
    const auto current = m_repo.findByUid(assessmentUid);
    if (!current)
        return Result<Domain::Assessment, ApplicationError>::failure(
            {ErrorCode::NotFound, "assessment not found", {}, false});
    if (current->status != Domain::AssessmentStatus::Draft)
        return Result<Domain::Assessment, ApplicationError>::failure(
            {ErrorCode::Conflict, "only draft assessment can become ready", {}, false});
    if (m_repo.itemsOf(assessmentUid).empty())
        return Result<Domain::Assessment, ApplicationError>::failure(
            {ErrorCode::Validation, "assessment has no items", {}, false});

    Domain::Assessment updated = *current;
    updated.status = Domain::AssessmentStatus::Ready;
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Assessment, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::Assessment, ApplicationError>::success(std::move(updated));
}

Result<Domain::AssessmentAttempt, ApplicationError> AssessmentUseCases::submitAttempt(
    const Domain::Uid &assessmentUid, const SubmitInput &input)
{
    if (input.idempotencyKey.empty())
        return Result<Domain::AssessmentAttempt, ApplicationError>::failure(
            {ErrorCode::Validation, "idempotency key required", {}, false});
    if (m_repo.existsAttemptKey(input.idempotencyKey))
        return Result<Domain::AssessmentAttempt, ApplicationError>::failure(
            {ErrorCode::Conflict, "duplicate attempt idempotency key", {}, false});

    const auto assessment = m_repo.findByUid(assessmentUid);
    if (!assessment)
        return Result<Domain::AssessmentAttempt, ApplicationError>::failure(
            {ErrorCode::NotFound, "assessment not found", {}, false});
    if (assessment->status != Domain::AssessmentStatus::Ready
        && assessment->status != Domain::AssessmentStatus::InProgress)
        return Result<Domain::AssessmentAttempt, ApplicationError>::failure(
            {ErrorCode::Conflict, "assessment is not open for attempts", {}, false});

    Domain::AssessmentAttempt attempt;
    attempt.uid = m_uids.next();
    attempt.assessmentId = assessmentUid;
    attempt.startedAt = m_clock.utcIso();
    attempt.answerJson = input.answerJson;
    attempt.evidenceAssetUid = input.evidenceAssetUid;
    attempt.selfRating = input.selfRating;
    attempt.idempotencyKey = input.idempotencyKey;
    const auto saved = m_repo.insertAttempt(attempt);
    if (!saved.ok)
        return Result<Domain::AssessmentAttempt, ApplicationError>::failure(saved.error);

    // 状态推进：ready → in_progress
    Domain::Assessment updated = *assessment;
    updated.status = Domain::AssessmentStatus::InProgress;
    m_repo.update(updated, assessment->revision);

    return Result<Domain::AssessmentAttempt, ApplicationError>::success(std::move(attempt));
}

Result<std::vector<Domain::AssessmentResult>, ApplicationError> AssessmentUseCases::scoreAttempt(
    const Domain::Uid &assessmentUid, int expectedRevision, const ScoreInput &input)
{
    const auto assessment = m_repo.findByUid(assessmentUid);
    if (!assessment)
        return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
            {ErrorCode::NotFound, "assessment not found", {}, false});
    const auto attempt = m_repo.findAttempt(input.attemptId);
    if (!attempt)
        return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
            {ErrorCode::NotFound, "attempt not found", {}, false});
    if (attempt->assessmentId != assessmentUid)
        return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
            {ErrorCode::Validation, "attempt does not belong to assessment", {}, false});

    // 已评分过的同一 scorer 不得重复（UNIQUE(attempt_id,item_id,scorer) 兜底）
    const auto existing = m_repo.resultsOfAttempt(input.attemptId);
    for (const auto &result : existing)
        if (result.scorer == input.scorer)
            return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
                {ErrorCode::Conflict, "this scorer already scored the attempt", {}, false});

    std::vector<Domain::AssessmentResult> created;
    for (const auto &itemScore : input.itemScores) {
        Domain::AssessmentResult result;
        result.uid = m_uids.next();
        result.attemptId = input.attemptId;
        result.itemId = itemScore.itemId;
        result.mastery = itemScore.mastery;
        result.score = itemScore.score;
        result.feedback = itemScore.feedback;
        result.scorer = input.scorer;
        result.confidence = input.confidence;
        result.createdAt = m_clock.utcIso();
        const auto saved = m_repo.insertResult(result);
        if (!saved.ok)
            return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
                saved.error);
        created.push_back(std::move(result));
    }

    // 提交作答时间 + assessment → scored
    Domain::Assessment updated = *assessment;
    updated.status = Domain::AssessmentStatus::Scored;
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::failure(
            saved.error);

    Audit::record({"user", {}, "assessment.scored", "assessment", assessmentUid.value(),
                   "{\"attempt\":\"" + input.attemptId.value() + "\"}"});
    return Result<std::vector<Domain::AssessmentResult>, ApplicationError>::success(
        std::move(created));
}

Result<Domain::AssessmentResult, ApplicationError> AssessmentUseCases::confirmResult(
    const Domain::Uid &resultUid)
{
    const auto saved = m_repo.confirmResult(resultUid);
    if (!saved.ok)
        return Result<Domain::AssessmentResult, ApplicationError>::failure(saved.error);
    Domain::AssessmentResult placeholder;
    placeholder.uid = resultUid;
    placeholder.confirmedByUser = true;
    return Result<Domain::AssessmentResult, ApplicationError>::success(std::move(placeholder));
}

} // namespace PersonOS::Application
