#pragma once

#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"
#include "application/ports/RouteRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/route/Route.h"

// 路线用例（DD-001 §5.2 ProposeRoute/ConfirmRoute；requirements R1.4/R1.5）
// 不变量：AI 生成的是候选（draft/proposed）；只有用户确认后成为 confirmed 并
// 指向当前版本；调整产生新版本，旧版本永久保留。
namespace PersonOS::Application {

class RouteUseCases
{
public:
    struct ProposeInput
    {
        Domain::Uid goalId;
        std::string rationale;          // 为什么这样规划（拆分依据，R1.4）
        std::string evidenceSummary;    // 知识来源摘要
        std::string assumptionsJson;    // 前提假设（JSON）
        std::vector<Domain::RouteStage> stages;
        std::string createdBy = "ai";   // ai 候选 / user 手建
    };

    struct ProposeOutput
    {
        Domain::Route route;
        int versionNo = 0;
        std::string versionUid;         // 版本 UID（确认时写入 current_version_uid）
    };

    RouteUseCases(RouteRepository &routes, GoalRepository &goals, UuidPort &uids,
                  const Domain::Clock &clock);

    Result<ProposeOutput, ApplicationError> proposeRoute(const ProposeInput &input);

    // 用户确认：候选 → confirmed，current_version_no 指向最新版本，记录确认时间。
    // 期望修订号不匹配返回 Conflict（防陈旧确认覆盖新版本）。
    Result<Domain::Route, ApplicationError> confirmRoute(const Domain::Uid &routeUid,
                                                         int expectedRevision);

private:
    RouteRepository &m_routes;
    GoalRepository &m_goals;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
