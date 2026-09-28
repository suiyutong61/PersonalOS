#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/RetentionRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/assessment/Retention.h"
#include "domain/foundation/Clock.h"

// 保持抽查/遗忘调度用例（架构 4.3.6、DR-021；requirements R3.4.1）
// 纪律：调度算法与参数不写死——每次计算间隔必须携带来自论文库/方法库/领域配置
// 的参数与来源标识；程序执行频率界限与状态规则。推迟不判定为能力下降；
// 抽查结果只影响保持状态与下次安排，不修改已完成 MEL 进度。
namespace PersonOS::Application {

class RetentionUseCases
{
public:
    struct CreateInput
    {
        Domain::Uid userId;
        std::optional<Domain::Uid> contentNodeUid;     // 与 assessmentItemUid 二选一
        std::optional<Domain::Uid> assessmentItemUid;
        std::string algorithmCode;                     // 调度方法标识（方法库引用/命名策略）
        std::string algorithmVersion;
        Domain::RetentionParameters parameters;        // 含 basisSource（必填，可追溯）
    };

    struct ApplyResultInput
    {
        std::string resultUid;                         // 本次抽查的三档结果（验收结果 uid）
        Domain::Mastery mastery;
        Domain::RetentionParameters parameters;        // 与上次相同或重新解析，均须带依据
    };

    RetentionUseCases(RetentionRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    // 建立调度（首次间隔 = parameters.baseIntervalMin）
    Result<Domain::RetentionSchedule, ApplicationError> createSchedule(
        const CreateInput &input);

    // 应用抽查结果：记录 last_result，按算法参数计算下一次间隔与到期时间
    Result<Domain::RetentionSchedule, ApplicationError> applyResult(
        const Domain::Uid &scheduleUid, const ApplyResultInput &input, int expectedRevision);

    // 推迟（必须推迟到未来）；不改变间隔、掌握表现与历史结果
    Result<Domain::RetentionSchedule, ApplicationError> postpone(
        const Domain::Uid &scheduleUid, const std::string &untilIso, int expectedRevision);

    // 关闭/恢复非关键抽查（active 开关；关闭后不再进入到期候选）
    Result<Domain::RetentionSchedule, ApplicationError> setActive(
        const Domain::Uid &scheduleUid, bool active, int expectedRevision);

    Result<Domain::RetentionSchedule, ApplicationError> findByUid(
        const Domain::Uid &scheduleUid);

    // 到期候选：active 且 next_due_at <= now；先按到期时间升序，
    // 再按最近表现（三档结果弱→强）优先，供执行与上层（AI）再排序
    Result<std::vector<RetentionCandidate>, ApplicationError> dueCandidates(
        const Domain::Uid &userId, const std::string &nowIso, int limit);

    Result<std::vector<Domain::RetentionSchedule>, ApplicationError> listForUser(
        const Domain::Uid &userId);

private:
    RetentionRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
