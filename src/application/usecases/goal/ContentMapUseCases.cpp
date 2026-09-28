#include "application/usecases/goal/ContentMapUseCases.h"

#include "application/audit/Audit.h"

#include <algorithm>
#include <sstream>

namespace PersonOS::Application {

namespace {

ApplicationError validation(const std::string &message)
{
    return {ErrorCode::Validation, message, {}, false};
}

ApplicationError notFound(const std::string &message)
{
    return {ErrorCode::NotFound, message, {}, false};
}

// 口径说明：等权/加权 + 跳过节点不计入分母
std::string caliberFor(int totalNodes, int skippedNodes, bool equalWeight)
{
    std::ostringstream out;
    out << "按 " << (totalNodes - skippedNodes) << " 个节点"
        << (equalWeight ? "等权" : "加权") << "计算";
    if (skippedNodes > 0)
        out << "，跳过 " << skippedNodes << " 个不计入";
    return out.str();
}

} // namespace

ContentMapUseCases::ContentMapUseCases(ContentMapRepository &repo, GoalRepository &goals,
                                       UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_goals(goals), m_uids(uids), m_clock(clock)
{}

Result<Domain::ContentMap, ApplicationError> ContentMapUseCases::createMap(
    const CreateMapInput &input)
{
    const auto goal = m_goals.findByUid(input.goalId);
    if (!goal)
        return Result<Domain::ContentMap, ApplicationError>::failure(
            notFound("goal not found"));
    if (goal->userId != input.userId)
        return Result<Domain::ContentMap, ApplicationError>::failure(
            {ErrorCode::Permission, "goal does not belong to user", {}, false});

    Domain::ContentMap map;
    map.uid = m_uids.next();
    map.goalId = input.goalId;
    map.title = input.title;
    map.sourceType = input.sourceType;
    map.sourceRef = input.sourceRef;
    map.status = Domain::ContentMapStatus::Draft;
    map.createdAt = m_clock.utcIso();
    map.updatedAt = map.createdAt;
    if (!map.isValid())
        return Result<Domain::ContentMap, ApplicationError>::failure(
            validation("content map invalid"));

    const auto saved = m_repo.insertMap(map);
    if (!saved.ok)
        return Result<Domain::ContentMap, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "content_map.created", "content_map", map.uid.value(),
                   "{\"title\":\"" + map.title + "\"}"});
    return Result<Domain::ContentMap, ApplicationError>::success(std::move(map));
}

Result<std::vector<Domain::ContentNode>, ApplicationError> ContentMapUseCases::addNodes(
    const Domain::Uid &mapUid, const AddNodesInput &input)
{
    const auto map = m_repo.findMapByUid(mapUid);
    if (!map)
        return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
            notFound("content map not found"));
    if (map->status != Domain::ContentMapStatus::Draft)
        return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
            validation("only draft map accepts new nodes"));
    if (input.nodes.empty())
        return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
            validation("no nodes to add"));

    // 父节点必须已存在于同一地图（按 uid 引用，不解析空引用）
    std::vector<Domain::Uid> knownNodes;
    for (const auto &existing : m_repo.nodesOfMap(mapUid))
        knownNodes.push_back(existing.uid);
    for (const auto &proto : input.nodes) {
        if (proto.parentNodeId && !proto.parentNodeId->empty()
            && std::find(knownNodes.begin(), knownNodes.end(), *proto.parentNodeId)
                   == knownNodes.end())
            return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
                validation("parent node does not belong to the same map"));
    }

    // 同父节点下序号唯一：SQLite UNIQUE 不约束 NULL 父节点的重复，应用层补判
    for (const auto &proto : input.nodes) {
        const std::string protoParent = proto.parentNodeId ? proto.parentNodeId->value() : "";
        for (const auto &existing : m_repo.nodesOfMap(mapUid)) {
            const std::string existingParent =
                existing.parentNodeId ? existing.parentNodeId->value() : "";
            if (protoParent == existingParent && proto.sequenceNo == existing.sequenceNo)
                return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
                    {ErrorCode::Conflict,
                     "duplicate (parent, sequence) in content map", {}, false});
        }
    }

    std::vector<Domain::ContentNode> created;
    for (const auto &proto : input.nodes) {
        Domain::ContentNode node = proto;
        node.uid = m_uids.next();
        node.mapId = mapUid;
        node.createdAt = m_clock.utcIso();
        node.updatedAt = node.createdAt;
        if (!node.isValid())
            return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
                validation("content node invalid (title/nodeType required, weight>0)"));
        const auto saved = m_repo.insertNode(node);
        if (!saved.ok)
            return Result<std::vector<Domain::ContentNode>, ApplicationError>::failure(
                saved.error);
        created.push_back(std::move(node));
    }
    return Result<std::vector<Domain::ContentNode>, ApplicationError>::success(
        std::move(created));
}

Result<Domain::ContentMap, ApplicationError> ContentMapUseCases::confirmMap(
    const Domain::Uid &mapUid, int expectedRevision)
{
    const auto current = m_repo.findMapByUid(mapUid);
    if (!current)
        return Result<Domain::ContentMap, ApplicationError>::failure(
            notFound("content map not found"));
    if (current->status != Domain::ContentMapStatus::Draft)
        return Result<Domain::ContentMap, ApplicationError>::failure(
            validation("only draft map can be confirmed"));
    if (m_repo.nodesOfMap(mapUid).empty())
        return Result<Domain::ContentMap, ApplicationError>::failure(
            validation("empty map cannot be confirmed"));

    Domain::ContentMap updated = *current;
    updated.status = Domain::ContentMapStatus::Confirmed;
    updated.updatedAt = m_clock.utcIso();
    const auto saved = m_repo.updateMap(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::ContentMap, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "content_map.confirmed", "content_map",
                   updated.uid.value(), "{}"});
    return Result<Domain::ContentMap, ApplicationError>::success(std::move(updated));
}

Result<Domain::ContentProgress, ApplicationError> ContentMapUseCases::updateProgress(
    const Domain::Uid &nodeUid, const UpdateProgressInput &input)
{
    if (input.idempotencyKey.empty())
        return Result<Domain::ContentProgress, ApplicationError>::failure(
            validation("idempotency key required"));
    if (input.progress < 0.0 || input.progress > 1.0)
        return Result<Domain::ContentProgress, ApplicationError>::failure(
            validation("progress must be within [0,1]"));

    const auto node = m_repo.findNodeByUid(nodeUid);
    if (!node)
        return Result<Domain::ContentProgress, ApplicationError>::failure(
            notFound("content node not found"));
    const auto map = m_repo.findMapByUid(node->mapId);
    if (!map)
        return Result<Domain::ContentProgress, ApplicationError>::failure(
            notFound("content map not found"));
    if (map->status != Domain::ContentMapStatus::Confirmed)
        return Result<Domain::ContentProgress, ApplicationError>::failure(
            validation("progress requires a confirmed map"));

    // 状态与进度一致：completed=1；not_started/skipped=0；in_progress∈[0,1]
    double effective = input.progress;
    switch (input.state) {
    case Domain::ContentNodeState::Completed:
        if (input.progress != 1.0)
            return Result<Domain::ContentProgress, ApplicationError>::failure(
                validation("completed node must have progress 1.0"));
        break;
    case Domain::ContentNodeState::NotStarted:
    case Domain::ContentNodeState::Skipped:
        effective = 0.0;
        break;
    case Domain::ContentNodeState::InProgress:
        break;
    }

    // 追加式事实先行：幂等键冲突时整次更新失败（重试安全，DB-06）
    const auto eventSaved = m_repo.appendProgressEvent(
        m_uids.next(), input.userId, map->goalId, nodeUid, node->title, effective,
        input.idempotencyKey);
    if (!eventSaved.ok)
        return Result<Domain::ContentProgress, ApplicationError>::failure(eventSaved.error);

    Domain::ContentProgress progress;
    progress.nodeId = nodeUid;
    progress.userId = input.userId;
    progress.state = input.state;
    progress.progress = effective;
    progress.updatedAt = m_clock.utcIso();
    progress.sourceEventUid = std::nullopt;   // 由仓储回填事件 uid
    const auto saved = m_repo.upsertProgress(progress);
    if (!saved.ok)
        return Result<Domain::ContentProgress, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "content_map.progress_updated", "content_node",
                   nodeUid.value(),
                   "{\"state\":\"" + Domain::toString(progress.state) + "\"}"});
    return Result<Domain::ContentProgress, ApplicationError>::success(std::move(progress));
}

namespace {

Domain::CoverageSummary summarize(std::vector<Domain::ContentProgressRow> rows,
                                  const Domain::Uid &targetUid, bool perGoal)
{
    Domain::CoverageSummary summary;
    summary.targetUid = targetUid;
    summary.perGoal = perGoal;

    bool equalWeight = true;
    double firstWeight = -1.0;
    for (const auto &row : rows) {
        ++summary.totalNodes;
        if (firstWeight < 0.0)
            firstWeight = row.weight;
        if (row.weight != firstWeight)
            equalWeight = false;
        switch (row.state) {
        case Domain::ContentNodeState::Completed:
            ++summary.completedNodes;
            summary.totalWeight += row.weight;
            summary.coveredWeight += row.weight;
            break;
        case Domain::ContentNodeState::InProgress:
            ++summary.inProgressNodes;
            summary.totalWeight += row.weight;
            summary.coveredWeight += row.weight * row.progress;
            break;
        case Domain::ContentNodeState::Skipped:
            ++summary.skippedNodes;
            break;   // 不计入口径分母
        case Domain::ContentNodeState::NotStarted:
            summary.totalWeight += row.weight;
            break;
        }
    }

    if (summary.totalWeight > 0.0)
        summary.coverage = summary.coveredWeight / summary.totalWeight;
    else
        summary.coverage = 0.0;
    summary.caliberText =
        caliberFor(summary.totalNodes, summary.skippedNodes, equalWeight);
    return summary;
}

} // namespace

Result<Domain::CoverageSummary, ApplicationError> ContentMapUseCases::coverage(
    const Domain::Uid &mapUid, const Domain::Uid &userId)
{
    const auto map = m_repo.findMapByUid(mapUid);
    if (!map)
        return Result<Domain::CoverageSummary, ApplicationError>::failure(
            notFound("content map not found"));
    if (map->status != Domain::ContentMapStatus::Confirmed)
        return Result<Domain::CoverageSummary, ApplicationError>::failure(
            validation("coverage requires a confirmed map"));
    return Result<Domain::CoverageSummary, ApplicationError>::success(
        summarize(m_repo.progressRowsOfMap(mapUid, userId), mapUid, false));
}

Result<Domain::CoverageSummary, ApplicationError> ContentMapUseCases::coverageByGoal(
    const Domain::Uid &goalUid, const Domain::Uid &userId)
{
    if (!m_goals.findByUid(goalUid))
        return Result<Domain::CoverageSummary, ApplicationError>::failure(
            notFound("goal not found"));

    std::vector<Domain::ContentProgressRow> rows;
    for (const auto &map : m_repo.mapsOfGoal(goalUid)) {
        if (map.status != Domain::ContentMapStatus::Confirmed)
            continue;   // 未确认地图不作为进度依据
        const auto mapRows = m_repo.progressRowsOfMap(map.uid, userId);
        rows.insert(rows.end(), mapRows.begin(), mapRows.end());
    }
    return Result<Domain::CoverageSummary, ApplicationError>::success(
        summarize(std::move(rows), goalUid, true));
}

Result<Domain::ContentMap, ApplicationError> ContentMapUseCases::findMap(
    const Domain::Uid &mapUid)
{
    const auto map = m_repo.findMapByUid(mapUid);
    if (!map)
        return Result<Domain::ContentMap, ApplicationError>::failure(
            notFound("content map not found"));
    return Result<Domain::ContentMap, ApplicationError>::success(*map);
}

Result<std::vector<Domain::ContentNode>, ApplicationError> ContentMapUseCases::nodesOf(
    const Domain::Uid &mapUid)
{
    return Result<std::vector<Domain::ContentNode>, ApplicationError>::success(
        m_repo.nodesOfMap(mapUid));
}

} // namespace PersonOS::Application
