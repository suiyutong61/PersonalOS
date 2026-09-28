#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domain/foundation/Uid.h"

// 路线聚合根（DD-001 §3；数据库设计 §3.3 routes_v3/route_versions_v3/route_stages_v3）
// 不变量：AI 生成的是候选版本；只有用户确认（userConfirmedAt 非空）才能成为
// confirmed/active 路线；每次调整产生新版本，旧版本永久保留。
namespace PersonOS::Domain {

enum class RouteStatus { Draft, Proposed, Confirmed, Active, Superseded, Completed };

inline std::string toString(RouteStatus s)
{
    switch (s) {
    case RouteStatus::Draft: return "draft";
    case RouteStatus::Proposed: return "proposed";
    case RouteStatus::Confirmed: return "confirmed";
    case RouteStatus::Active: return "active";
    case RouteStatus::Superseded: return "superseded";
    case RouteStatus::Completed: return "completed";
    }
    return "draft";
}

inline std::optional<RouteStatus> routeStatusFrom(std::string_view value)
{
    if (value == "draft") return RouteStatus::Draft;
    if (value == "proposed") return RouteStatus::Proposed;
    if (value == "confirmed") return RouteStatus::Confirmed;
    if (value == "active") return RouteStatus::Active;
    if (value == "superseded") return RouteStatus::Superseded;
    if (value == "completed") return RouteStatus::Completed;
    return std::nullopt;
}

struct RouteStage
{
    Uid uid;
    std::optional<Uid> parentStageId;   // 支持层级阶段
    std::string title;
    std::string description;
    int sequenceNo = 0;
    std::string completionRuleJson;     // 完成判定规则（口径可解释）
    std::optional<int> estimatedEffortMin;
    int revision = 1;
};

struct RouteVersion
{
    std::string uid;                    // 版本 UID（routes_v3.current_version_uid 引用）
    int versionNo = 1;
    std::string rationale;              // 为什么这样规划（R1.4 拆分依据）
    std::string evidenceSummary;        // 引用的知识来源摘要
    std::string assumptionsJson;        // 前提假设
    std::optional<std::string> userConfirmedAt;  // 用户确认时间（UTC ISO）
    std::vector<RouteStage> stages;
};

struct Route
{
    Uid uid;
    Uid goalId;
    RouteStatus status = RouteStatus::Draft;
    std::optional<std::string> currentVersionUid;   // 当前版本（仅确认后非空）
    std::string createdBy;              // user / ai / system
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const { return !uid.empty() && !goalId.empty(); }
};

} // namespace PersonOS::Domain
