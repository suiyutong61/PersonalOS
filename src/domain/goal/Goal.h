#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 目标聚合根（DD-001 §3；数据库设计 §3.3 goals_v3）
// 不变量：目标层级无环（由 GoalUseCases 在写前校验）；
// 掌握标准优先由用户决定（user_defined_level），不确定时由 AI 引导。
namespace PersonOS::Domain {

enum class GoalStatus { Draft, Active, Paused, Achieved, Abandoned, Archived };

inline std::string toString(GoalStatus s)
{
    switch (s) {
    case GoalStatus::Draft: return "draft";
    case GoalStatus::Active: return "active";
    case GoalStatus::Paused: return "paused";
    case GoalStatus::Achieved: return "achieved";
    case GoalStatus::Abandoned: return "abandoned";
    case GoalStatus::Archived: return "archived";
    }
    return "draft";
}

inline std::optional<GoalStatus> goalStatusFrom(std::string_view value)
{
    if (value == "draft") return GoalStatus::Draft;
    if (value == "active") return GoalStatus::Active;
    if (value == "paused") return GoalStatus::Paused;
    if (value == "achieved") return GoalStatus::Achieved;
    if (value == "abandoned") return GoalStatus::Abandoned;
    if (value == "archived") return GoalStatus::Archived;
    return std::nullopt;
}

struct Goal
{
    Uid uid;
    Uid userId;
    std::optional<Uid> parentGoalId;    // 空 = 根目标
    Uid domainManifestId;               // 关联领域清单（学习等）
    std::string title;
    std::string description;
    std::string goalType;               // 领域内类型（领域中立，不写死课程）
    GoalStatus status = GoalStatus::Draft;
    int priority = 50;
    std::optional<std::string> targetAt;      // YYYY-MM-DD
    std::string desiredLevelJson;             // 完成标准（JSON，含口径与判定规则；
                                              // 用户自定义时内容来自用户表述）
    bool userDefinedLevel = false;            // 0/1：用户是否已自定义掌握标准（DB CHECK）
    int sortOrder = 0;
    int revision = 1;
    std::string createdAt;                    // UTC ISO-8601
    std::string updatedAt;

    bool isRoot() const { return !parentGoalId.has_value(); }
    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !domainManifestId.empty() && !title.empty();
    }
};

} // namespace PersonOS::Domain
