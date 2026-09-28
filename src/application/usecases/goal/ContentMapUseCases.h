#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/ContentMapRepository.h"
#include "application/ports/GoalRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/goal/ContentMap.h"

// 长期内容地图用例（requirements R3.2.1；架构 4.3.5、DR-020）
// 纪律：未确认的目录节点不得成为长期进度、提问与验收范围的依据；
// 内容覆盖只表示"学到哪里"，与 MEL 执行率、能力验收分离；
// 进度更新同时写追加式事件（历史不覆盖）与当前状态（可查询投影）。
namespace PersonOS::Application {

class ContentMapUseCases
{
public:
    struct CreateMapInput
    {
        Domain::Uid userId;          // 用于校验目标归属
        Domain::Uid goalId;
        std::string title;
        std::string sourceType;      // user_document/web/generated/manual
        std::optional<std::string> sourceRef;
    };

    struct AddNodesInput
    {
        std::vector<Domain::ContentNode> nodes;   // mapId 由用例填入；weight>0
    };

    struct UpdateProgressInput
    {
        Domain::Uid userId;
        Domain::ContentNodeState state;
        double progress = 0.0;       // completed=1；not_started/skipped=0；in_progress∈[0,1]
        std::string idempotencyKey;  // 必填，唯一（追加式进度事件）
    };

    ContentMapUseCases(ContentMapRepository &repo, GoalRepository &goals, UuidPort &uids,
                       const Domain::Clock &clock);

    // 创建目录地图（draft；确认后才能作为进度依据）
    Result<Domain::ContentMap, ApplicationError> createMap(const CreateMapInput &input);

    // 向草稿地图追加节点（同地图内 (parent, sequence) 唯一；权重>0）
    Result<std::vector<Domain::ContentNode>, ApplicationError> addNodes(
        const Domain::Uid &mapUid, const AddNodesInput &input);

    // 确认目录（draft → confirmed；空目录不得确认）
    Result<Domain::ContentMap, ApplicationError> confirmMap(const Domain::Uid &mapUid,
                                                            int expectedRevision);

    // 更新节点进度：追加式事件 + 当前状态 upsert；仅已确认地图
    Result<Domain::ContentProgress, ApplicationError> updateProgress(
        const Domain::Uid &nodeUid, const UpdateProgressInput &input);

    // 单地图内容覆盖率（口径：跳过节点不计入分母；in_progress 按比例计）
    Result<Domain::CoverageSummary, ApplicationError> coverage(
        const Domain::Uid &mapUid, const Domain::Uid &userId);

    // 按目标聚合全部已确认地图的覆盖率
    Result<Domain::CoverageSummary, ApplicationError> coverageByGoal(
        const Domain::Uid &goalUid, const Domain::Uid &userId);

    Result<Domain::ContentMap, ApplicationError> findMap(const Domain::Uid &mapUid);
    Result<std::vector<Domain::ContentNode>, ApplicationError> nodesOf(
        const Domain::Uid &mapUid);

private:
    ContentMapRepository &m_repo;
    GoalRepository &m_goals;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
