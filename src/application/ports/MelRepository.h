#pragma once

#include <optional>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/mel/Mel.h"

// MEL 仓储端口（DD-001 §5.3）
// 转移与进度事件带幂等键；更新走乐观并发 revision。
namespace PersonOS::Application {

class MelRepository
{
public:
    virtual ~MelRepository() = default;

    virtual std::optional<Domain::Mel> findByUid(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::Mel &mel) = 0;
    virtual SaveResult update(const Domain::Mel &mel, int expectedRevision) = 0;

    // 任务
    virtual SaveResult insertTask(const Domain::MelTask &task) = 0;
    virtual SaveResult updateTask(const Domain::MelTask &task, int expectedRevision) = 0;
    virtual std::vector<Domain::MelTask> tasksOf(const Domain::Uid &melId) = 0;

    // 转移（幂等键唯一；mel_revision_after 记录转移后的修订号）
    virtual SaveResult appendTransition(const Domain::MelTransition &transition) = 0;
    // 进度事件（追加式事实；幂等键唯一，DB-06）
    virtual SaveResult appendProgressEvent(const Domain::Mel &mel, const Domain::MelTask &task,
                                           double amount, const std::string &note,
                                           const std::string &idempotencyKey,
                                           const std::string &unit = "progress",
                                           const std::string &actorType = "user") = 0;
    virtual bool existsIdempotencyKey(const std::string &key) = 0;
    // 是否已发生过某类转移（结算幂等：settle 只结算一次，E2E-05）
    virtual bool hasTransition(const Domain::Uid &melId, const std::string &trigger) = 0;

    // 按窗口查找应结算/逾期的 MEL（调度器与启动恢复用）
    virtual std::vector<Domain::Mel> findDue(const std::string &nowIso, int limit) = 0;
    // R2 检测只读投影：最近一次进度时间与实际耗时（分钟）
    virtual std::optional<std::string> lastProgressAtIso(const Domain::Uid &melId) = 0;
    virtual int actualMinutesOf(const Domain::Uid &melId) = 0;
    // 逐任务方法建议（R3.1）：绑定与查询
    virtual SaveResult insertTaskMethod(const Domain::MelTaskMethod &method) = 0;
    virtual std::vector<Domain::MelTaskMethod> taskMethodsOf(const Domain::Uid &melId) = 0;
    // 计划预测（追加式；校准读取最新未废弃一条）
    virtual SaveResult insertPrediction(const Domain::MelPrediction &prediction) = 0;
    virtual std::optional<Domain::MelPrediction> latestPredictionOf(
        const Domain::Uid &melId) = 0;
    // 活跃 MEL（页面展示用，只读）
    virtual std::vector<Domain::Mel> findActive(const Domain::Uid &userId, int limit) = 0;
    // 用户的全部 MEL（含候选/历史，最新在前；首个 MEL 候选展示用）
    virtual std::vector<Domain::Mel> findByUser(const Domain::Uid &userId, int limit) = 0;
};

} // namespace PersonOS::Application
