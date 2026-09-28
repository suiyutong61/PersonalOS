#include "application/usecases/assessment/RetentionUseCases.h"

#include "application/audit/Audit.h"

#include <algorithm>

namespace PersonOS::Application {

namespace {

ApplicationError validation(const std::string &message)
{
    return {ErrorCode::Validation, message, {}, false};
}

ApplicationError notFound(const std::string &message)
{
    return {ErrorCode::NotFound, message, {}, false};
}

// 三档结果弱→强的排序权重：表现弱的优先复查
int masteryPriority(Domain::Mastery mastery)
{
    switch (mastery) {
    case Domain::Mastery::NotRecalled: return 0;
    case Domain::Mastery::Prompted: return 1;
    case Domain::Mastery::Fluent: return 2;
    case Domain::Mastery::NotApplicable: return 3;
    }
    return 3;
}

// 只接受与库内一致的规范 UTC ISO 格式（YYYY-MM-DDTHH:MM:SSZ），
// 保证字符串比较等于时间比较（数据库设计 §1.3）。
bool isCanonicalUtcIso(std::string_view value)
{
    if (value.size() != 20 || value.back() != 'Z')
        return false;
    return value[4] == '-' && value[7] == '-' && value[10] == 'T' && value[13] == ':'
           && value[16] == ':';
}

} // namespace

RetentionUseCases::RetentionUseCases(RetentionRepository &repo, UuidPort &uids,
                                     const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<Domain::RetentionSchedule, ApplicationError> RetentionUseCases::createSchedule(
    const CreateInput &input)
{
    if (input.userId.empty())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("userId required"));
    if (input.contentNodeUid.has_value() == input.assessmentItemUid.has_value())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("exactly one of contentNodeUid/assessmentItemUid required"));
    if (input.algorithmCode.empty() || input.algorithmVersion.empty())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("algorithm code and version required"));
    if (!input.parameters.isValid())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("retention parameters invalid or missing basis source"));

    Domain::RetentionSchedule schedule;
    schedule.uid = m_uids.next();
    schedule.userId = input.userId;
    schedule.contentNodeId = input.contentNodeUid;
    schedule.assessmentItemId = input.assessmentItemUid;
    schedule.intervalMin = input.parameters.baseIntervalMin;
    schedule.nextDueAt = m_clock.utcIsoPlusMinutes(input.parameters.baseIntervalMin);
    schedule.algorithmCode = input.algorithmCode;
    schedule.algorithmVersion = input.algorithmVersion;
    schedule.active = true;
    schedule.createdAt = m_clock.utcIso();
    schedule.updatedAt = schedule.createdAt;
    if (!schedule.isValid())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("retention schedule invalid"));

    const auto saved = m_repo.insert(schedule);
    if (!saved.ok)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "retention.schedule_created", "retention_schedule",
                   schedule.uid.value(),
                   "{\"algorithm\":\"" + schedule.algorithmCode + "\"}"});
    return Result<Domain::RetentionSchedule, ApplicationError>::success(std::move(schedule));
}

Result<Domain::RetentionSchedule, ApplicationError> RetentionUseCases::applyResult(
    const Domain::Uid &scheduleUid, const ApplyResultInput &input, int expectedRevision)
{
    if (input.resultUid.empty())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("result uid required"));
    if (!input.parameters.isValid())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("retention parameters invalid or missing basis source"));

    const auto current = m_repo.findByUid(scheduleUid);
    if (!current)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            notFound("retention schedule not found"));

    // 确定性计算：参数无效（越界/缺依据）时不猜测、不默认
    const auto nextInterval = Domain::nextRetentionInterval(
        input.parameters, current->intervalMin, input.mastery);
    if (!nextInterval)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("interval computation failed with given parameters"));

    Domain::RetentionSchedule updated = *current;
    updated.lastResultUid = input.resultUid;
    updated.intervalMin = *nextInterval;
    updated.nextDueAt = m_clock.utcIsoPlusMinutes(*nextInterval);
    updated.updatedAt = m_clock.utcIso();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "retention.updated", "retention_schedule",
                   updated.uid.value(),
                   "{\"next_due\":\"" + updated.nextDueAt + "\"}"});
    return Result<Domain::RetentionSchedule, ApplicationError>::success(std::move(updated));
}

Result<Domain::RetentionSchedule, ApplicationError> RetentionUseCases::postpone(
    const Domain::Uid &scheduleUid, const std::string &untilIso, int expectedRevision)
{
    // 推迟必须推迟到未来；推迟本身不改变间隔、稳定性或最近表现
    if (!isCanonicalUtcIso(untilIso))
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("postpone target must be canonical UTC ISO (YYYY-MM-DDTHH:MM:SSZ)"));
    if (untilIso <= m_clock.utcIso())
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            validation("postpone target must be in the future"));

    const auto current = m_repo.findByUid(scheduleUid);
    if (!current)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            notFound("retention schedule not found"));

    Domain::RetentionSchedule updated = *current;
    updated.nextDueAt = untilIso;
    updated.updatedAt = m_clock.utcIso();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::RetentionSchedule, ApplicationError>::success(std::move(updated));
}

Result<Domain::RetentionSchedule, ApplicationError> RetentionUseCases::setActive(
    const Domain::Uid &scheduleUid, bool active, int expectedRevision)
{
    const auto current = m_repo.findByUid(scheduleUid);
    if (!current)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            notFound("retention schedule not found"));

    Domain::RetentionSchedule updated = *current;
    updated.active = active;
    updated.updatedAt = m_clock.utcIso();
    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    return Result<Domain::RetentionSchedule, ApplicationError>::success(std::move(updated));
}

Result<Domain::RetentionSchedule, ApplicationError> RetentionUseCases::findByUid(
    const Domain::Uid &scheduleUid)
{
    const auto found = m_repo.findByUid(scheduleUid);
    if (!found)
        return Result<Domain::RetentionSchedule, ApplicationError>::failure(
            notFound("retention schedule not found"));
    return Result<Domain::RetentionSchedule, ApplicationError>::success(*found);
}

Result<std::vector<RetentionCandidate>, ApplicationError> RetentionUseCases::dueCandidates(
    const Domain::Uid &userId, const std::string &nowIso, int limit)
{
    if (limit <= 0)
        return Result<std::vector<RetentionCandidate>, ApplicationError>::failure(
            validation("limit must be positive"));

    auto candidates = m_repo.dueCandidates(userId, nowIso, limit);
    // 到期时间升序；同到期时间时三档表现弱→强优先（以往作答与遗忘表现）
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const RetentionCandidate &a, const RetentionCandidate &b) {
                         if (a.schedule.nextDueAt != b.schedule.nextDueAt)
                             return a.schedule.nextDueAt < b.schedule.nextDueAt;
                         return masteryPriority(a.lastMastery.value_or(Domain::Mastery::NotApplicable))
                                < masteryPriority(b.lastMastery.value_or(Domain::Mastery::NotApplicable));
                     });
    return Result<std::vector<RetentionCandidate>, ApplicationError>::success(
        std::move(candidates));
}

Result<std::vector<Domain::RetentionSchedule>, ApplicationError> RetentionUseCases::listForUser(
    const Domain::Uid &userId)
{
    auto schedules = m_repo.listForUser(userId);
    std::stable_sort(schedules.begin(), schedules.end(),
                     [](const Domain::RetentionSchedule &a, const Domain::RetentionSchedule &b) {
                         return a.nextDueAt < b.nextDueAt;
                     });
    return Result<std::vector<Domain::RetentionSchedule>, ApplicationError>::success(
        std::move(schedules));
}

} // namespace PersonOS::Application
