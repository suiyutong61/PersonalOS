#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/MelRepository.h"
#include "application/ports/StateRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/ai/Ai.h"
#include "domain/foundation/Clock.h"

// R5 外围监督与主动建议（requirements R5.1–R5.4；架构 4.3/4.8）
// 纪律：默认静默——只有明确风险/缺口/机会才生成建议；频率受安静窗口约束；
// 建议不自动执行（user_status=pending，用户接受/拒绝/稍后处理）；
// 知识支持程度如实标记（本模块为确定性触发，source_mode 由调用方注入）。
namespace PersonOS::Application {

// 决策记录仓储的最小接口（SqlAiRepository 实现；避免建议模块依赖 AI 全接口）
class DecisionStore
{
public:
    virtual ~DecisionStore() = default;
    virtual SaveResult insertDecision(const Domain::DecisionRecord &decision) = 0;
    virtual std::optional<Domain::DecisionRecord> findDecision(const Domain::Uid &uid) = 0;
    virtual SaveResult updateDecisionStatus(const Domain::Uid &uid,
                                            const std::string &userStatus,
                                            const std::optional<std::string> &selectedJson) = 0;
    // 最近决策（按类型过滤；v1 单用户）
    virtual std::vector<Domain::DecisionRecord> listDecisions(const std::string &decisionType,
                                                              int limit) = 0;
};

class AdviceUseCases
{
public:
    struct AdviceConfig
    {
        int quietWindowMinutes = 0;    // 同一对象/类型的建议安静窗口
        int maxActiveAdvice = 0;       // 待处理建议上限（超出不再新增）
        std::string basisSource;       // 必填：触发与频控依据
        bool isValid() const
        {
            return quietWindowMinutes > 0 && maxActiveAdvice > 0 && !basisSource.empty();
        }
    };

    struct AdviceCandidate
    {
        std::string text;              // 建议内容（可读）
        std::string basis;             // 依据（检测事实/知识来源）
        std::string level = "info";    // info / warning / risk
    };

    AdviceUseCases(DecisionStore &decisions, StateRepository &states,
                   MelRepository &mels, UuidPort &uids, const Domain::Clock &clock);

    // 触发一轮评估：活跃检测、逾期 MEL、弱验收结果 → 生成待确认建议；
    // 安静窗口内不重复；无触发保持静默。返回新增建议数。
    Result<int, ApplicationError> evaluate(const Domain::Uid &userId,
                                           const std::string &nowIso,
                                           const AdviceConfig &config,
                                           Domain::SourceMode sourceMode);

    // 待处理建议列表（最新在前）
    Result<std::vector<Domain::DecisionRecord>, ApplicationError> pendingAdvice(int limit);

    // 用户反馈：accepted / rejected / snoozed（R5.4；选择与效果进入状态库
    // 由决策记录 user_status 表达，审计追加）
    Result<Domain::DecisionRecord, ApplicationError> respond(const Domain::Uid &adviceUid,
                                                             const std::string &response,
                                                             const std::string &note);

private:
    DecisionStore &m_decisions;
    StateRepository &m_states;
    MelRepository &m_mels;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
