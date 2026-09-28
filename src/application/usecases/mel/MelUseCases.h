#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/MelRepository.h"
#include "application/ports/RouteRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/mel/Mel.h"

// MEL 用例（DD-001 §5.2；requirements R3）
// 状态机路径与守卫在 §4.1/§4.2；结算幂等键 settle:{mel_uid}:{revision}。
// AI 只生成候选（CreateMELProposal → awaiting_confirmation），用户确认后激活。
namespace PersonOS::Application {

class MelUseCases
{
public:
    struct CreateInput
    {
        Domain::Uid userId;
        Domain::Uid goalId;           // 必填：本轮主要目标
        std::optional<Domain::Uid> routeVersionId;   // 可选：路线版本
        Domain::Uid manifestVersionId;   // 必填：领域配置版本
        std::string title;
        std::string plannedStartAt;   // UTC ISO-8601
        std::string plannedEndAt;
        std::string timezoneId = "Asia/Shanghai";
        std::string settlementMode = "deadline";
        int capacityMin = 0;
        int reserveMin = 0;
        std::string rationale;
        std::optional<std::string> knowledgeSnapshotUid;
        std::vector<Domain::MelTask> tasks;   // 仅标题/描述/工作量/规则/必做标记有意义
    };

    struct CreateOutput
    {
        Domain::Mel mel;
    };

    MelUseCases(MelRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    // AI/用户生成候选：Draft（AI 生成）→ 提交确认 → AwaitingConfirmation
    Result<CreateOutput, ApplicationError> createMelProposal(const CreateInput &input);
    Result<Domain::Mel, ApplicationError> submitForConfirmation(const Domain::Uid &melUid,
                                                                int expectedRevision);

    // 用户确认并激活（守卫：确认前必须已有任务、时间窗、容量依据；AI 候选被新版本取代则冲突）
    Result<Domain::Mel, ApplicationError> confirmAndActivate(const Domain::Uid &melUid,
                                                             int expectedRevision);

    Result<Domain::Mel, ApplicationError> pauseMel(const Domain::Uid &melUid, int expectedRevision);
    Result<Domain::Mel, ApplicationError> resumeMel(const Domain::Uid &melUid, int expectedRevision);
    Result<Domain::Mel, ApplicationError> cancelMel(const Domain::Uid &melUid, int expectedRevision,
                                                    const std::string &reason);

    // 进度上报（幂等键唯一）：更新任务进度，全部必需任务达到完成规则后
    // 必须显式调用 completeExecution（不自动跨越状态）。
    struct ProgressInput
    {
        Domain::Uid taskUid;
        double progress = 0.0;          // 0..1，本次上报后的任务进度
        std::optional<int> actualMinutes;
        std::string note;
        std::string idempotencyKey;     // 必填，唯一
    };
    Result<Domain::MelTask, ApplicationError> recordProgress(const Domain::Uid &melUid,
                                                             const ProgressInput &input);

    // 执行完成：全部 required 任务达到完成规则
    Result<Domain::Mel, ApplicationError> completeExecution(const Domain::Uid &melUid,
                                                            int expectedRevision);

    // 逾期标记（Deadline 到达或启动恢复发现）：active → overdue
    Result<Domain::Mel, ApplicationError> markOverdue(const Domain::Uid &melUid,
                                                      int expectedRevision);

    // 结算（幂等 settle:{mel_uid}:{revision}）：
    // execution_complete/overdue → settling → （需要验收 → awaiting_assessment : reviewing）
    struct SettleOutput
    {
        Domain::Mel mel;
        bool requiresAssessment = true;
    };
    Result<SettleOutput, ApplicationError> settleMel(const Domain::Uid &melUid,
                                                     int expectedRevision);

    // 验收已提交或用户明确跳过（记录原因）→ reviewing
    Result<Domain::Mel, ApplicationError> proceedToReviewing(const Domain::Uid &melUid,
                                                             int expectedRevision,
                                                             const std::string &reason);

    // 复盘结论确认 → closed（下一动作由调用方写入复盘）
    Result<Domain::Mel, ApplicationError> closeMel(const Domain::Uid &melUid,
                                                   int expectedRevision,
                                                   const std::string &nextAction);

private:
    Result<Domain::Mel, ApplicationError> transition(const Domain::Uid &melUid,
                                                     Domain::MelState to,
                                                     int expectedRevision,
                                                     const std::string &trigger,
                                                     const std::string &actorType,
                                                     const std::string &reason);

    MelRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
