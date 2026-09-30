#pragma once

#include <optional>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/ports/GoalRepository.h"
#include "domain/foundation/Uid.h"
#include "domain/route/Route.h"
#include "domain/route/StageDetail.h"

// 路线仓储端口（DD-001 §5.3；阶段详情为 2026-09-30 追加）
namespace PersonOS::Application {

// 阶段定位信息（locateStage 读取模型）：把阶段联回所属版本与路线，
// 供阶段详情用例校验"路线已确认才能生成阶段详情"等门禁。
struct StageLocation
{
    Domain::RouteStage stage;   // 含 completionRuleJson（key_contents/关系 JSON）
    Domain::Uid routeUid;
    std::string routeVersionUid;
    int routeVersionNo = 0;
    Domain::RouteStatus routeStatus = Domain::RouteStatus::Draft;
};

class RouteRepository
{
public:
    virtual ~RouteRepository() = default;

    virtual std::optional<Domain::Route> findByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::Route> findByGoal(const Domain::Uid &goalId) = 0;
    virtual SaveResult insert(const Domain::Route &route) = 0;
    virtual SaveResult update(const Domain::Route &route, int expectedRevision) = 0;

    // 版本与阶段：版本只追加；阶段随版本一次写入。成功返回生成的版本 UID。
    virtual Result<std::string, ApplicationError> insertVersion(
        const Domain::Uid &routeId, const Domain::RouteVersion &version) = 0;
    virtual std::vector<Domain::RouteVersion> versionsOf(const Domain::Uid &routeId) = 0;

    // 用户确认：记录版本确认时间（用户确认是不可覆盖的历史事实，只追加不撤销）
    virtual SaveResult markVersionConfirmed(const Domain::Uid &routeId, int versionNo,
                                            const std::string &confirmedAtIso) = 0;

    // ===== 阶段详情（route_stage_details_v11 / route_stage_materials_v11）=====
    // 详情版本只追加（version_no 递增），仅用户确认的版本生效；
    // 资料绑定在阶段上（跨详情版本存续），用户逐条接受/拒绝。
    virtual std::optional<StageLocation> locateStage(const Domain::Uid &stageUid) = 0;
    virtual std::vector<Domain::StageDetailVersion> stageDetailVersionsOf(
        const Domain::Uid &stageUid) = 0;
    // 成功返回生成的详情版本 UID。
    virtual Result<std::string, ApplicationError> insertStageDetail(
        const Domain::StageDetailVersion &detail) = 0;
    // 用户确认是不可覆盖的历史事实，只追加不撤销；重复确认返回 Conflict。
    virtual SaveResult markStageDetailConfirmed(const Domain::Uid &stageUid, int versionNo,
                                                const std::string &confirmedAtIso) = 0;
    // 资料建议：同 (stage, item) 重建议时更新建议内容；已 accepted 的用户决定保持不动。
    virtual SaveResult upsertStageMaterial(const Domain::StageMaterialBinding &material) = 0;
    virtual std::vector<Domain::StageMaterialBinding> stageMaterialsOf(
        const Domain::Uid &stageUid) = 0;
    virtual SaveResult updateStageMaterialChoice(const Domain::Uid &stageUid,
                                                 const std::string &itemUid,
                                                 const std::string &choice) = 0;
};

} // namespace PersonOS::Application
