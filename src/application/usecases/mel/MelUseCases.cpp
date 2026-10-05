#include "application/usecases/mel/MelUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

MelUseCases::MelUseCases(MelRepository &repo, UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<MelUseCases::CreateOutput, ApplicationError> MelUseCases::createMelProposal(
    const CreateInput &input)
{
    Domain::Mel mel;
    mel.uid = m_uids.next();
    mel.userId = input.userId;
    mel.goalId = input.goalId;
    mel.routeVersionId = input.routeVersionId;
    mel.manifestVersionId = input.manifestVersionId;
    mel.title = input.title;
    mel.state = Domain::MelState::Draft;
    mel.plannedStartAt = input.plannedStartAt;
    mel.plannedEndAt = input.plannedEndAt;
    mel.timezoneId = input.timezoneId;
    mel.settlementMode = input.settlementMode;
    mel.capacityMin = input.capacityMin;
    mel.reserveMin = input.reserveMin;
    mel.rationale = input.rationale;
    mel.knowledgeSnapshotUid = input.knowledgeSnapshotUid;

    if (!mel.isValid())
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation,
             "mel window/capacity invalid (end must be after start, reserve<=capacity)",
             {}, false});
    if (input.tasks.empty())
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "mel must contain at least one task", {}, false});
    for (const auto &t : input.tasks)
        if (t.title.empty() || t.progress < 0.0 || t.progress > 1.0)
            return Result<CreateOutput, ApplicationError>::failure(
                {ErrorCode::Validation, "task title/progress invalid", {}, false});

    const auto saved = m_repo.insert(mel);
    if (!saved.ok)
        return Result<CreateOutput, ApplicationError>::failure(saved.error);

    for (const auto &proto : input.tasks) {
        Domain::MelTask task = proto;
        task.uid = m_uids.next();
        task.melId = mel.uid;
        task.state = Domain::MelTaskState::Pending;
        const auto taskSaved = m_repo.insertTask(task);
        if (!taskSaved.ok)
            return Result<CreateOutput, ApplicationError>::failure(taskSaved.error);
    }
    Audit::record({"user", {}, "mel.proposal_created", "mel", mel.uid.value(),
                   "{\"title\":\"" + mel.title + "\",\"tasks\":" + std::to_string(input.tasks.size())
                       + "}"});
    return Result<CreateOutput, ApplicationError>::success(CreateOutput{std::move(mel)});
}

Result<Domain::Mel, ApplicationError> MelUseCases::submitForConfirmation(
    const Domain::Uid &melUid, int expectedRevision)
{
    return transition(melUid, Domain::MelState::AwaitingConfirmation, expectedRevision,
                      "submit_for_confirmation", "user", {});
}

Result<Domain::Mel, ApplicationError> MelUseCases::confirmAndActivate(
    const Domain::Uid &melUid, int expectedRevision)
{
    const auto current = m_repo.findByUid(melUid);
    if (!current)
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});
    if (current->state != Domain::MelState::AwaitingConfirmation)
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::Conflict, "only awaiting_confirmation mel can be activated", {}, false});

    // 守卫（DD-001 §4.2）：至少一个任务、时间窗、容量依据（rationale 非空）
    if (m_repo.tasksOf(melUid).empty())
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::Validation, "mel has no tasks", {}, false});
    if (current->capacityMin <= 0)
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::Validation, "capacity basis missing", {}, false});

    return transition(melUid, Domain::MelState::Active, expectedRevision,
                      "confirm_and_activate", "user", {});
}

Result<Domain::Mel, ApplicationError> MelUseCases::pauseMel(const Domain::Uid &melUid,
                                                            int expectedRevision)
{
    return transition(melUid, Domain::MelState::Paused, expectedRevision, "pause", "user", {});
}

Result<Domain::Mel, ApplicationError> MelUseCases::resumeMel(const Domain::Uid &melUid,
                                                             int expectedRevision)
{
    return transition(melUid, Domain::MelState::Active, expectedRevision, "resume", "user", {});
}

Result<Domain::Mel, ApplicationError> MelUseCases::cancelMel(const Domain::Uid &melUid,
                                                             int expectedRevision,
                                                             const std::string &reason)
{
    return transition(melUid, Domain::MelState::Cancelled, expectedRevision, "cancel", "user",
                      reason);
}

Result<Domain::MelTask, ApplicationError> MelUseCases::recordProgress(
    const Domain::Uid &melUid, const ProgressInput &input)
{
    if (input.idempotencyKey.empty())
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::Validation, "idempotency key required", {}, false});
    if (input.progress < 0.0 || input.progress > 1.0)
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::Validation, "progress must be within 0..1", {}, false});

    const auto mel = m_repo.findByUid(melUid);
    if (!mel)
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});
    if (mel->state != Domain::MelState::Active && mel->state != Domain::MelState::Paused)
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::Conflict, "progress can only be recorded while mel is active/paused",
             {}, false});

    // 幂等：同一键重复上报返回同一结果（DB-06）
    if (m_repo.existsIdempotencyKey(input.idempotencyKey))
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::Conflict, "duplicate idempotency key", {}, false});

    std::optional<Domain::MelTask> target;
    for (auto &t : m_repo.tasksOf(melUid))
        if (t.uid == input.taskUid)
            target = t;
    if (!target)
        return Result<Domain::MelTask, ApplicationError>::failure(
            {ErrorCode::NotFound, "task not found in mel", {}, false});

    Domain::MelTask updated = *target;
    updated.progress = input.progress;
    if (input.progress >= 1.0) {
        updated.state = Domain::MelTaskState::Completed;
        updated.completedAt = m_clock.utcIso();
    } else if (input.progress > 0.0 && updated.state == Domain::MelTaskState::Pending) {
        updated.state = Domain::MelTaskState::Active;
    }

    const auto saved = m_repo.updateTask(updated, target->revision);
    if (!saved.ok)
        return Result<Domain::MelTask, ApplicationError>::failure(saved.error);
    updated.revision = target->revision + 1;

    // 进度事件（追加式事实，幂等键唯一——DB-06）
    const auto event = m_repo.appendProgressEvent(
        *mel, updated, input.progress, input.note, input.idempotencyKey);
    if (!event.ok)
        return Result<Domain::MelTask, ApplicationError>::failure(event.error);

    Audit::record({"user", {}, "mel.progress_recorded", "mel", melUid.value(),
                   "{\"task\":\"" + updated.uid.value() + "\",\"progress\":"
                       + std::to_string(updated.progress) + "}"});

    return Result<Domain::MelTask, ApplicationError>::success(std::move(updated));
}

Result<Domain::Mel, ApplicationError> MelUseCases::completeExecution(
    const Domain::Uid &melUid, int expectedRevision)
{
    const auto current = m_repo.findByUid(melUid);
    if (!current)
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});

    // 守卫：全部 required 任务必须达到完成规则（progress>=1.0）
    for (const auto &task : m_repo.tasksOf(melUid))
        if (task.required && task.progress < 1.0)
            return Result<Domain::Mel, ApplicationError>::failure(
                {ErrorCode::Validation, "required tasks not all completed", {}, false});

    return transition(melUid, Domain::MelState::ExecutionComplete, expectedRevision,
                      "complete_execution", "user", {});
}

Result<Domain::Mel, ApplicationError> MelUseCases::markOverdue(const Domain::Uid &melUid,
                                                               int expectedRevision)
{
    return transition(melUid, Domain::MelState::Overdue, expectedRevision, "mark_overdue",
                      "system", {});
}

Result<MelUseCases::SettleOutput, ApplicationError> MelUseCases::settleMel(
    const Domain::Uid &melUid, int expectedRevision)
{
    // 幂等（DD-001 §4.2 / E2E-05）：settle 转移只发生一次；重复触发返回同一结果
    const auto current = m_repo.findByUid(melUid);
    if (!current)
        return Result<SettleOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});
    if (m_repo.hasTransition(melUid, "settle")) {
        // 已结算过：返回当前状态（幂等，不再产生转移）
        SettleOutput output;
        output.mel = *current;
        output.requiresAssessment = current->state == Domain::MelState::AwaitingAssessment
                                    || current->state == Domain::MelState::Settling;
        return Result<SettleOutput, ApplicationError>::success(std::move(output));
    }
    if (current->state != Domain::MelState::ExecutionComplete
        && current->state != Domain::MelState::Overdue)
        return Result<SettleOutput, ApplicationError>::failure(
            {ErrorCode::Conflict, "mel is not ready to settle", {}, false});

    // settling → awaiting_assessment（能力验收路径）；skip 由 proceedToReviewing 完成
    // 转移幂等键 = "settle:{mel_uid}:{revision}"，与上方预检查一致
    const auto settling = transition(melUid, Domain::MelState::Settling, expectedRevision,
                                     "settle", "system", {});
    if (!settling)
        return Result<SettleOutput, ApplicationError>::failure(settling.error());
    const auto assessing = transition(melUid, Domain::MelState::AwaitingAssessment,
                                      expectedRevision + 1, "settle", "system", {});
    if (!assessing)
        return Result<SettleOutput, ApplicationError>::failure(assessing.error());

    SettleOutput output;
    output.mel = assessing.value();
    output.requiresAssessment = true;
    return Result<SettleOutput, ApplicationError>::success(std::move(output));
}

Result<Domain::Mel, ApplicationError> MelUseCases::proceedToReviewing(
    const Domain::Uid &melUid, int expectedRevision, const std::string &reason)
{
    return transition(melUid, Domain::MelState::Reviewing, expectedRevision,
                      "proceed_to_reviewing", "user", reason);
}

Result<Domain::Mel, ApplicationError> MelUseCases::closeMel(const Domain::Uid &melUid,
                                                            int expectedRevision,
                                                            const std::string &nextAction)
{
    return transition(melUid, Domain::MelState::Closed, expectedRevision,
                      "close_review", "user", nextAction);
}

Result<Domain::Mel, ApplicationError> MelUseCases::transition(const Domain::Uid &melUid,
                                                              Domain::MelState to,
                                                              int expectedRevision,
                                                              const std::string &trigger,
                                                              const std::string &actorType,
                                                              const std::string &reason)
{
    const auto current = m_repo.findByUid(melUid);
    if (!current)
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});
    if (!Domain::MelStateMachine::canTransition(current->state, to))
        return Result<Domain::Mel, ApplicationError>::failure(
            {ErrorCode::Conflict,
             "illegal state transition " + Domain::toString(current->state) + " -> "
                 + Domain::toString(to),
             {}, false});

    Domain::Mel updated = *current;
    updated.state = to;
    // 状态与对应的事实时间必须在同一次乐观锁更新中落库，避免“状态已变更、
    // 时间戳写入失败”的半成功状态。
    const auto occurredAt = m_clock.utcIso();
    if (current->state == Domain::MelState::AwaitingConfirmation
        && to == Domain::MelState::Active) {
        updated.confirmedAt = occurredAt;
        updated.activatedAt = occurredAt;
    }
    if (to == Domain::MelState::Closed)
        updated.closedAt = occurredAt;
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Mel, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;

    // 转移记录（追加式审计；幂等键唯一）
    Domain::MelTransition t;
    t.uid = m_uids.next();
    t.melId = melUid;
    t.fromState = current->state;
    t.toState = to;
    t.trigger = trigger;
    t.actorType = actorType;
    t.reason = reason;
    t.idempotencyKey = trigger + ":" + melUid.value() + ":" + std::to_string(expectedRevision);
    t.occurredAt = occurredAt;
    t.melRevisionAfter = updated.revision;
    m_repo.appendTransition(t);

    // 审计事件（requirements 10.6.1：状态变更追加式记录，可追溯、不含秘密）
    Audit::record({actorType, {}, "mel.transitioned", "mel", melUid.value(),
                   "{\"from\":\"" + Domain::toString(current->state) + "\",\"to\":\""
                       + Domain::toString(to) + "\",\"trigger\":\"" + trigger + "\"}"});

    return Result<Domain::Mel, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
