#include "application/usecases/route/RouteStageDetailUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

RouteStageDetailUseCases::RouteStageDetailUseCases(RouteRepository &routes,
                                                   DecisionStore &decisions,
                                                   const Domain::Clock &clock)
    : m_routes(routes), m_decisions(decisions), m_clock(clock)
{}

Result<void, ApplicationError> RouteStageDetailUseCases::confirmStageDetail(
    const Domain::Uid &stageUid, int expectedVersionNo)
{
    const auto location = m_routes.locateStage(stageUid);
    if (!location)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::NotFound, "stage not found", {}, false});
    if (location->routeStatus != Domain::RouteStatus::Confirmed
        && location->routeStatus != Domain::RouteStatus::Active)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Conflict, "stage detail requires a confirmed route", {}, false});

    const auto versions = m_routes.stageDetailVersionsOf(stageUid);
    if (versions.empty())
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Conflict, "stage has no detail version", {}, false});
    const auto &latest = versions.back();
    if (latest.versionNo != expectedVersionNo)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Conflict, "expected version is not the latest", {}, false});
    if (latest.userConfirmedAt)
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Conflict, "latest version already confirmed", {}, false});

    // 用户确认是不可覆盖的历史事实：先记录确认时间（仓储只追加不撤销）。
    const auto confirmed =
        m_routes.markStageDetailConfirmed(stageUid, expectedVersionNo, m_clock.utcIso());
    if (!confirmed.ok)
        return Result<void, ApplicationError>::failure(confirmed.error);

    // 决策记录接线：把该阶段最新的 Pending 详情决策标记为 accepted。
    // 无匹配记录时跳过（历史数据容错，不阻断确认）。
    for (const auto &decision : m_decisions.listDecisions("stage_detail", 20))
        if (decision.aggregateType == "route_stage"
            && decision.aggregateUid == stageUid.value()
            && decision.userStatus == Domain::DecisionUserStatus::Pending) {
            m_decisions.updateDecisionStatus(decision.uid, "accepted", std::nullopt);
            break;
        }

    Audit::record({"user", {}, "route.stage_detail_confirmed", "route_stage",
                   stageUid.value(), "{\"version\":" + std::to_string(expectedVersionNo)
                                         + "}"});
    return Result<void, ApplicationError>::success();
}

Result<void, ApplicationError> RouteStageDetailUseCases::respondStageMaterial(
    const Domain::Uid &stageUid, const std::string &itemUid, const std::string &choice)
{
    if (choice != "accepted" && choice != "rejected")
        return Result<void, ApplicationError>::failure(
            {ErrorCode::Validation, "choice must be accepted or rejected", {}, false});

    // 行不存在 → NotFound（资料只能来自 AI 建议，不在此处插入）
    const auto saved = m_routes.updateStageMaterialChoice(stageUid, itemUid, choice);
    if (!saved.ok)
        return Result<void, ApplicationError>::failure(saved.error);

    Audit::record({"user", {}, "route.stage_material_responded", "route_stage",
                   stageUid.value(), "{\"item_uid\":\"" + itemUid + "\",\"choice\":\""
                                         + choice + "\"}"});
    return Result<void, ApplicationError>::success();
}

} // namespace PersonOS::Application
