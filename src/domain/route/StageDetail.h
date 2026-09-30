#pragma once

#include <optional>
#include <string>

#include "domain/foundation/Uid.h"

// 路线阶段详情（DD-001 补充：route_stage_details_v11 / route_stage_materials_v11）
// 回答"如何真正完成这一阶段"：可验证结果、少量内部任务及顺序、项目/练习、
// 完成标准、资料与用户—AI 调整。
// 不变量：AI 生成的是候选版本；只有用户确认（userConfirmedAt 非空）才正式采用；
// 每次调整产生新版本（version_no 递增），旧版本永久保留；资料绑定在阶段上
// （跨详情版本存续），用户逐条接受/拒绝（userChoice）。
namespace PersonOS::Domain {

struct StageDetailVersion
{
    Uid uid;
    Uid stageUid;
    int versionNo = 1;
    std::string outcomesJson;   // 可验证结果列表
    std::string tasksJson;      // 少量内部任务及顺序
    std::string projectsJson;   // 项目/练习
    std::string criteriaJson;   // 完成标准
    std::string rationale;      // 为什么这样安排（拆分依据）
    std::string createdBy;      // ai / user / system
    std::optional<std::string> userConfirmedAt;  // 用户确认时间（UTC ISO）
    int revision = 1;
};

struct StageMaterialBinding
{
    Uid stageUid;
    std::string knowledgeItemUid;    // 知识条目 UID（非外键：条目可物理删除）
    std::string knowledgeVersionUid; // 建议采用的版本
    int rank = 0;
    std::string reason;              // 推荐理由
    std::string userChoice = "pending";  // pending / accepted / rejected
    std::string createdBy;           // ai / user / system
};

} // namespace PersonOS::Domain
