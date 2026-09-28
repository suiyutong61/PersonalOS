#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/foundation/Uid.h"

// MEL 聚合（DD-001 §3/§4；数据库设计 §4.1 mels_v4/mel_tasks_v4/mel_transitions_v4）
// 不变量：
// - 状态转移只允许状态机声明的路径（§4.1）；
// - 时间窗、容量与休息余量必须明确（capacity/reserve，reserve<=capacity）；
// - 全部必需任务达到完成规则才可 execution_complete；
// - 结算使用幂等键 settle:{mel_uid}:{revision}；
// - 用户确认后（confirmed_at 非空）才能 active。
namespace PersonOS::Domain {

enum class MelState {
    Draft,
    AwaitingConfirmation,
    Active,
    Paused,
    ExecutionComplete,
    Overdue,
    Settling,
    AwaitingAssessment,
    Reviewing,
    Closed,
    Cancelled,
};

inline std::string toString(MelState s)
{
    switch (s) {
    case MelState::Draft: return "draft";
    case MelState::AwaitingConfirmation: return "awaiting_confirmation";
    case MelState::Active: return "active";
    case MelState::Paused: return "paused";
    case MelState::ExecutionComplete: return "execution_complete";
    case MelState::Overdue: return "overdue";
    case MelState::Settling: return "settling";
    case MelState::AwaitingAssessment: return "awaiting_assessment";
    case MelState::Reviewing: return "reviewing";
    case MelState::Closed: return "closed";
    case MelState::Cancelled: return "cancelled";
    }
    return "draft";
}

inline std::optional<MelState> melStateFrom(std::string_view value)
{
    if (value == "draft") return MelState::Draft;
    if (value == "awaiting_confirmation") return MelState::AwaitingConfirmation;
    if (value == "active") return MelState::Active;
    if (value == "paused") return MelState::Paused;
    if (value == "execution_complete") return MelState::ExecutionComplete;
    if (value == "overdue") return MelState::Overdue;
    if (value == "settling") return MelState::Settling;
    if (value == "awaiting_assessment") return MelState::AwaitingAssessment;
    if (value == "reviewing") return MelState::Reviewing;
    if (value == "closed") return MelState::Closed;
    if (value == "cancelled") return MelState::Cancelled;
    return std::nullopt;
}

enum class MelTaskState { Pending, Active, Completed, Skipped, Cancelled };

inline std::string toString(MelTaskState s)
{
    switch (s) {
    case MelTaskState::Pending: return "pending";
    case MelTaskState::Active: return "active";
    case MelTaskState::Completed: return "completed";
    case MelTaskState::Skipped: return "skipped";
    case MelTaskState::Cancelled: return "cancelled";
    }
    return "pending";
}

inline std::optional<MelTaskState> melTaskStateFrom(std::string_view value)
{
    if (value == "pending") return MelTaskState::Pending;
    if (value == "active") return MelTaskState::Active;
    if (value == "completed") return MelTaskState::Completed;
    if (value == "skipped") return MelTaskState::Skipped;
    if (value == "cancelled") return MelTaskState::Cancelled;
    return std::nullopt;
}

// MEL 状态机（DD-001 §4.1）：返回 true = 转移被允许。
// 守卫（§4.2）由用例在执行前检查；本函数只编码路径合法性。
class MelStateMachine
{
public:
    static bool canTransition(MelState from, MelState to)
    {
        switch (from) {
        case MelState::Draft:
            return to == MelState::AwaitingConfirmation || to == MelState::Cancelled;
        case MelState::AwaitingConfirmation:
            return to == MelState::Draft || to == MelState::Active || to == MelState::Cancelled;
        case MelState::Active:
            return to == MelState::Paused || to == MelState::ExecutionComplete
                   || to == MelState::Overdue || to == MelState::Cancelled;
        case MelState::Paused:
            return to == MelState::Active || to == MelState::Cancelled;
        case MelState::ExecutionComplete:
        case MelState::Overdue:
            return to == MelState::Settling || to == MelState::Cancelled;
        case MelState::Settling:
            return to == MelState::AwaitingAssessment || to == MelState::Reviewing;
        case MelState::AwaitingAssessment:
            return to == MelState::Reviewing;
        case MelState::Reviewing:
            // 复盘结论确认 → closed；下一 MEL 候选待确认时当前 MEL 保持 reviewing
            return to == MelState::Closed;
        case MelState::Closed:
        case MelState::Cancelled:
            return false;   // 终态
        }
        return false;
    }

    static bool isTerminal(MelState s)
    {
        return s == MelState::Closed || s == MelState::Cancelled;
    }
};

struct Mel
{
    Uid uid;
    Uid userId;
    Uid goalId;                       // 必填：本轮主要目标（多目标经任务关联扩展）
    std::optional<Uid> routeVersionId;   // 路线版本（可选）
    Uid manifestVersionId;            // 领域配置版本（必填）
    std::string title;
    MelState state = MelState::Draft;
    std::string plannedStartAt;       // UTC ISO-8601
    std::string plannedEndAt;         // UTC ISO-8601；必须晚于 start
    std::string timezoneId;           // IANA，如 Asia/Shanghai
    std::string settlementMode = "deadline";   // deadline / startup / manual
    int capacityMin = 0;              // 计划容量（分钟）
    int reserveMin = 0;               // 休息余量（分钟），<= capacity
    std::string rationale;            // 为什么这样安排（可解释性）
    std::optional<std::string> knowledgeSnapshotUid;
    std::optional<std::string> confirmedAt;   // 用户确认时间
    std::optional<std::string> activatedAt;
    std::optional<std::string> closedAt;
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool hasValidWindow() const { return !plannedStartAt.empty() && plannedEndAt > plannedStartAt; }
    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !goalId.empty()
               && !manifestVersionId.empty() && !title.empty() && hasValidWindow()
               && reserveMin <= capacityMin;
    }
};

struct MelTask
{
    Uid uid;
    Uid melId;
    std::optional<Uid> parentTaskId;
    std::string title;
    std::string description;
    int sequenceNo = 0;
    bool required = true;
    int plannedEffortMin = 0;
    std::string completionRuleJson = "{}";   // 完成判定规则（口径可解释）
    MelTaskState state = MelTaskState::Pending;
    double progress = 0.0;                   // 0..1
    std::optional<std::string> completedAt;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !melId.empty() && !title.empty()
               && progress >= 0.0 && progress <= 1.0;
    }
    bool isDone() const { return state == MelTaskState::Completed; }
};

// 任务—方法绑定（mel_task_method_v4；R3.1 逐任务方法建议，DR-017）
// 方法必须来自检索召回的方法库条目（AI 只能选择召回候选项，不得凭空引用）。
struct MelTaskMethod
{
    Uid taskUid;
    std::string methodVersionUid;
    int rank = 0;
    std::string reason;                 // 匹配理由
    std::string applicabilityJson = "{}";   // 适用条件
    std::string userChoice = "pending";     // pending/accepted/rejected/tried

    bool isValid() const
    {
        return !taskUid.empty() && !methodVersionUid.empty();
    }
};

// 计划预测（mel_predictions_v4；DR-018/036 预测→实际→校准闭环的输入端）
// 预测值附带依据（知识支持状态/快照），只追加；校准用最新未废弃一条。
struct MelPrediction
{
    Uid uid;
    Uid melId;
    double predictedCompletion = 1.0;   // 0..1（计划目标的完成预期）
    int predictedEffortMin = 0;
    std::string riskLevel = "medium";   // low / medium / high
    std::string basisJson = "{}";       // 依据（source_mode/快照/模型）
    std::string createdAt;
    std::optional<std::string> supersededAt;

    bool isValid() const
    {
        return !uid.empty() && !melId.empty() && predictedCompletion >= 0.0
               && predictedCompletion <= 1.0 && predictedEffortMin >= 0;
    }
};

struct MelTransition
{
    Uid uid;
    Uid melId;
    MelState fromState;
    MelState toState;
    std::string trigger;              // 转移触发来源（用例名）
    std::string actorType;            // user / ai / system
    std::optional<std::string> actorRef;
    std::string reason;
    std::string idempotencyKey;       // 唯一
    std::string occurredAt;
    int melRevisionAfter = 1;
};

} // namespace PersonOS::Domain
