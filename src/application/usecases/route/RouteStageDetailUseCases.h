#pragma once

#include <string>

#include "application/foundation/Result.h"
#include "application/ports/RouteRepository.h"
#include "application/usecases/advice/AdviceUseCases.h"   // DecisionStore
#include "domain/foundation/Clock.h"

// 路线阶段详情用例（2026-09-30 追加；requirements R1.4/R1.5、DD-001 §5.4a）
// 不变量：AI 生成候选版本；只有用户确认（user_confirmed_at）才正式采用；
// 确认是不可覆盖的历史事实（只追加不撤销）；资料逐条接受/拒绝并写审计。
namespace PersonOS::Application {

class RouteStageDetailUseCases
{
public:
    RouteStageDetailUseCases(RouteRepository &routes, DecisionStore &decisions,
                             const Domain::Clock &clock);

    // 用户确认最新详情版本；期望版本号不是最新返回 Conflict（防陈旧确认）。
    Result<void, ApplicationError> confirmStageDetail(const Domain::Uid &stageUid,
                                                      int expectedVersionNo);

    // 用户对资料建议的决定：pending → accepted/rejected（允许改主意）。
    Result<void, ApplicationError> respondStageMaterial(const Domain::Uid &stageUid,
                                                        const std::string &itemUid,
                                                        const std::string &choice);

private:
    RouteRepository &m_routes;
    DecisionStore &m_decisions;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
