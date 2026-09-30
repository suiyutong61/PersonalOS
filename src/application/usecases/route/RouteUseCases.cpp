#include "application/usecases/route/RouteUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

RouteUseCases::RouteUseCases(RouteRepository &routes, GoalRepository &goals, UuidPort &uids,
                             const Domain::Clock &clock, DecisionStore *decisions)
    : m_routes(routes), m_goals(goals), m_uids(uids), m_clock(clock), m_decisions(decisions)
{}

Result<RouteUseCases::ProposeOutput, ApplicationError> RouteUseCases::proposeRoute(
    const ProposeInput &input)
{
    if (!m_goals.findByUid(input.goalId))
        return Result<ProposeOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "goal not found", {}, false});
    if (input.stages.empty())
        return Result<ProposeOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "route must contain at least one stage", {}, false});

    Domain::Route route;
    route.uid = m_uids.next();
    route.goalId = input.goalId;
    route.status = input.createdBy == "user" ? Domain::RouteStatus::Proposed
                                             : Domain::RouteStatus::Draft;
    route.createdBy = input.createdBy;

    const auto saved = m_routes.insert(route);
    if (!saved.ok)
        return Result<ProposeOutput, ApplicationError>::failure(saved.error);

    Domain::RouteVersion version;
    version.versionNo = 1;
    version.rationale = input.rationale;
    version.evidenceSummary = input.evidenceSummary;
    version.assumptionsJson = input.assumptionsJson;
    version.stages = input.stages;
    for (auto &stage : version.stages)
        stage.uid = m_uids.next();

    const auto versionSaved = m_routes.insertVersion(route.uid, version);
    if (!versionSaved)
        return Result<ProposeOutput, ApplicationError>::failure(versionSaved.error());

    Audit::record({"user", {}, "route.proposed", "route", route.uid.value(),
                   "{\"rationale\":\"" + input.rationale + "\"}"});
    ProposeOutput output;
    output.route = std::move(route);
    output.versionNo = 1;
    output.versionUid = versionSaved.value();
    return Result<ProposeOutput, ApplicationError>::success(std::move(output));
}

Result<Domain::Route, ApplicationError> RouteUseCases::confirmRoute(
    const Domain::Uid &routeUid, int expectedRevision)
{
    const auto current = m_routes.findByUid(routeUid);
    if (!current)
        return Result<Domain::Route, ApplicationError>::failure(
            {ErrorCode::NotFound, "route not found", {}, false});
    if (current->status != Domain::RouteStatus::Draft
        && current->status != Domain::RouteStatus::Proposed)
        return Result<Domain::Route, ApplicationError>::failure(
            {ErrorCode::Conflict, "only draft/proposed routes can be confirmed", {}, false});

    const auto versions = m_routes.versionsOf(routeUid);
    if (versions.empty())
        return Result<Domain::Route, ApplicationError>::failure(
            {ErrorCode::Conflict, "route has no version", {}, false});
    const int latestVersion = versions.back().versionNo;
    const std::string latestVersionUid = versions.back().uid;

    // 用户确认是不可覆盖的历史事实：先记录确认时间，再推进路线状态。
    const auto confirmed = m_routes.markVersionConfirmed(
        routeUid, latestVersion, m_clock.utcIso());
    if (!confirmed.ok)
        return Result<Domain::Route, ApplicationError>::failure(confirmed.error);

    Domain::Route updated = *current;
    updated.status = Domain::RouteStatus::Confirmed;
    updated.currentVersionUid = latestVersionUid;
    const auto saved = m_routes.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Route, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;

    // 决策记录接线：把该路线最新的 Pending route_proposal 决策标记为 accepted，
    // 并把确认采用的候选原文写入 selected_json（与阶段详情确认同款；无匹配
    // 记录时跳过——用户手工路线/历史数据容错，不阻断确认）
    if (m_decisions)
        for (const auto &decision : m_decisions->listDecisions("route_proposal", 20))
            if (decision.aggregateType == "route" && decision.aggregateUid == routeUid.value()
                && decision.userStatus == Domain::DecisionUserStatus::Pending) {
                m_decisions->updateDecisionStatus(decision.uid, "accepted",
                                                  std::optional<std::string>(
                                                      decision.candidateJson));
                break;
            }

    Audit::record({"user", {}, "route.confirmed", "route", updated.uid.value(), "{}"});
    return Result<Domain::Route, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
