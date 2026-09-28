#include "application/usecases/advice/AdviceUseCases.h"

#include "application/audit/Audit.h"
#include "application/usecases/detection/DetectionUseCases.h"   // isoToEpochMinutes

namespace PersonOS::Application {

namespace {

// 建议与检测的映射：触发类型 → 建议文案模板（具体事实来自检测事件 valueJson）
struct TriggerSpec
{
    const char *definitionCode;   // 触发用的状态定义
    const char *aggregateType;
    const char *text;
    const char *level;
};

const TriggerSpec kTriggers[] = {
    {"subproblem_progress_lag", "mel", "本轮进度落后于计划节奏，建议结合原因选择调整",
     "warning"},
    {"subproblem_time_overrun", "mel", "实际耗时明显高于预计，建议重估任务耗时与方法",
     "warning"},
    {"subproblem_inactivity", "mel", "一段时间没有执行记录，先了解原因再决定是否调整",
     "info"},
    {"subproblem_weak_assessment", "assessment", "验收存在提示后仍不能完成的内容，建议安排复习",
     "warning"},
};

} // namespace

AdviceUseCases::AdviceUseCases(DecisionStore &decisions, StateRepository &states,
                               MelRepository &mels, UuidPort &uids,
                               const Domain::Clock &clock)
    : m_decisions(decisions), m_states(states), m_mels(mels), m_uids(uids), m_clock(clock)
{}

Result<int, ApplicationError> AdviceUseCases::evaluate(const Domain::Uid &userId,
                                                       const std::string &nowIso,
                                                       const AdviceConfig &config,
                                                       Domain::SourceMode sourceMode)
{
    if (!config.isValid())
        return Result<int, ApplicationError>::failure(
            {ErrorCode::Validation, "advice config invalid or missing basis source", {}, false});

    // 活跃检测读取（最近事件中挑子问题定义；安静窗口内同一对象不再重复建议）
    const auto detections = m_states.recentEvents(userId, 50);
    int created = 0;
    for (const auto &event : detections) {
        const TriggerSpec *spec = nullptr;
        for (const auto &trigger : kTriggers)
            if (event.definitionCode == trigger.definitionCode)
                spec = &trigger;
        if (!spec)
            continue;

        // 频控：同对象近 quietWindow 内已有一条该类型建议 → 静默
        bool recent = false;
        for (const auto &existing : m_decisions.listDecisions("advice", 200)) {
            if (existing.aggregateUid == event.uid.value()
                && existing.userStatus != Domain::DecisionUserStatus::Rejected) {
                const auto createdMinutes = isoToEpochMinutes(existing.createdAt);
                const auto nowMinutes = isoToEpochMinutes(nowIso);
                if (createdMinutes && nowMinutes
                    && *nowMinutes - *createdMinutes < config.quietWindowMinutes) {
                    recent = true;
                    break;
                }
            }
        }
        if (recent)
            continue;

        // 上限：待处理建议数量约束
        int pendingCount = 0;
        for (const auto &existing : m_decisions.listDecisions("advice", 200))
            if (existing.userStatus == Domain::DecisionUserStatus::Pending)
                ++pendingCount;
        if (pendingCount >= config.maxActiveAdvice)
            break;

        Domain::DecisionRecord decision;
        decision.uid = m_uids.next();
        decision.decisionType = "advice";
        decision.aggregateType = spec->aggregateType;
        decision.aggregateUid = event.uid.value();
        decision.inputSnapshotJson =
            "{\"definition\":\"" + event.definitionCode + "\",\"observed_at\":\""
            + event.observedAt + "\"}";
        decision.candidateJson =
            "{\"suggestions\":[{\"text\":\"" + std::string(spec->text)
            + "\",\"level\":\"" + spec->level + "\",\"basis\":\"" + config.basisSource
            + "\"}]}";
        decision.rationale = "由检测事件触发；依据：" + config.basisSource;
        decision.sourceMode = sourceMode;
        decision.warningJson = "{\"auto_execute\":false}";
        decision.userStatus = Domain::DecisionUserStatus::Pending;
        decision.createdAt = nowIso;
        const auto saved = m_decisions.insertDecision(decision);
        if (!saved.ok)
            return Result<int, ApplicationError>::failure(saved.error);
        Audit::record({"system", {}, "advice.generated", "decision", decision.uid.value(),
                       "{\"trigger\":\"" + event.definitionCode + "\"}"});
        ++created;
    }

    return Result<int, ApplicationError>::success(created);
}

Result<std::vector<Domain::DecisionRecord>, ApplicationError> AdviceUseCases::pendingAdvice(
    int limit)
{
    return Result<std::vector<Domain::DecisionRecord>, ApplicationError>::success(
        m_decisions.listDecisions("advice", limit));
}

Result<Domain::DecisionRecord, ApplicationError> AdviceUseCases::respond(
    const Domain::Uid &adviceUid, const std::string &response, const std::string &note)
{
    if (response != "accepted" && response != "rejected" && response != "snoozed")
        return Result<Domain::DecisionRecord, ApplicationError>::failure(
            {ErrorCode::Validation, "response must be accepted/rejected/snoozed", {}, false});

    const auto current = m_decisions.findDecision(adviceUid);
    if (!current)
        return Result<Domain::DecisionRecord, ApplicationError>::failure(
            {ErrorCode::NotFound, "advice not found", {}, false});

    std::optional<std::string> selectedJson;
    std::string storedStatus = response;
    if (response == "accepted")
        selectedJson = current->candidateJson;
    if (response == "snoozed")
        storedStatus = "pending";   // 稍后处理 = 保持待处理（状态枚举无 snoozed）
    const auto saved =
        m_decisions.updateDecisionStatus(adviceUid, storedStatus, selectedJson);
    if (!saved.ok)
        return Result<Domain::DecisionRecord, ApplicationError>::failure(saved.error);

    Domain::DecisionRecord updated = *current;
    if (response == "accepted")
        updated.userStatus = Domain::DecisionUserStatus::Accepted;
    else if (response == "rejected")
        updated.userStatus = Domain::DecisionUserStatus::Rejected;
    else
        updated.userStatus = Domain::DecisionUserStatus::Pending;   // snoozed：稍后处理
    updated.confirmedAt = m_clock.utcIso();
    Audit::record({"user", note, "advice.responded", "decision", adviceUid.value(),
                   "{\"response\":\"" + response + "\"}"});
    return Result<Domain::DecisionRecord, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
