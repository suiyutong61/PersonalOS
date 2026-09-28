#pragma once

#include <optional>
#include <vector>

#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/goal/ContentMap.h"

// 内容地图仓储端口（DD-001 §5.3；数据库设计 §3.3）
namespace PersonOS::Application {

class ContentMapRepository
{
public:
    virtual ~ContentMapRepository() = default;

    virtual std::optional<Domain::ContentMap> findMapByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::ContentMap> mapsOfGoal(const Domain::Uid &goalId) = 0;
    virtual SaveResult insertMap(const Domain::ContentMap &map) = 0;
    virtual SaveResult updateMap(const Domain::ContentMap &map, int expectedRevision) = 0;

    virtual std::optional<Domain::ContentNode> findNodeByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::ContentNode> nodesOfMap(const Domain::Uid &mapId) = 0;
    virtual SaveResult insertNode(const Domain::ContentNode &node) = 0;

    // 当前进度（每节点一行，UNIQUE(node,user)）；更新走 upsert
    virtual SaveResult upsertProgress(const Domain::ContentProgress &progress) = 0;
    virtual std::vector<Domain::ContentProgressRow> progressRowsOfMap(
        const Domain::Uid &mapId, const Domain::Uid &userId) = 0;

    // 追加式进度事件（progress_events_v4；幂等键唯一）——历史事实不覆盖
    virtual SaveResult appendProgressEvent(const Domain::Uid &eventUid,
                                           const Domain::Uid &userId,
                                           const Domain::Uid &goalId,
                                           const Domain::Uid &nodeUid,
                                           const std::string &nodeTitle, double amount,
                                           const std::string &idempotencyKey) = 0;
};

} // namespace PersonOS::Application
