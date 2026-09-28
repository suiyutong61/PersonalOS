#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 长期内容地图与双层进度（requirements R3.2.1；架构 4.3.5、DR-020；数据库设计 §3.3）
// 不变量：未经确认的目录不得成为长期进度、提问与验收范围的依据；
// 内容覆盖率表示"学到哪里"，不表示已经掌握（与 MEL 执行率、能力验收三者分离）。
namespace PersonOS::Domain {

enum class ContentMapStatus { Draft, Confirmed, Retired };

inline std::string toString(ContentMapStatus s)
{
    switch (s) {
    case ContentMapStatus::Draft: return "draft";
    case ContentMapStatus::Confirmed: return "confirmed";
    case ContentMapStatus::Retired: return "retired";
    }
    return "draft";
}

inline std::optional<ContentMapStatus> contentMapStatusFrom(std::string_view value)
{
    if (value == "draft") return ContentMapStatus::Draft;
    if (value == "confirmed") return ContentMapStatus::Confirmed;
    if (value == "retired") return ContentMapStatus::Retired;
    return std::nullopt;
}

struct ContentMap
{
    Uid uid;
    Uid goalId;
    std::string title;
    std::string sourceType;             // user_document/web/generated/manual
    std::optional<std::string> sourceRef;
    ContentMapStatus status = ContentMapStatus::Draft;
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const
    {
        return !uid.empty() && !goalId.empty() && !title.empty() && !sourceType.empty();
    }
};

struct ContentNode
{
    Uid uid;
    Uid mapId;
    std::optional<Uid> parentNodeId;    // 同地图内的父节点
    std::string title;
    std::string nodeType;               // chapter/section/topic/...
    int sequenceNo = 0;
    double weight = 1.0;                // >0；默认同层等权
    std::string locatorJson = "{}";     // 页码/小节等定位信息
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const
    {
        return !uid.empty() && !mapId.empty() && !title.empty() && !nodeType.empty()
               && weight > 0.0;
    }
};

enum class ContentNodeState { NotStarted, InProgress, Completed, Skipped };

inline std::string toString(ContentNodeState s)
{
    switch (s) {
    case ContentNodeState::NotStarted: return "not_started";
    case ContentNodeState::InProgress: return "in_progress";
    case ContentNodeState::Completed: return "completed";
    case ContentNodeState::Skipped: return "skipped";
    }
    return "not_started";
}

inline std::optional<ContentNodeState> contentNodeStateFrom(std::string_view value)
{
    if (value == "not_started") return ContentNodeState::NotStarted;
    if (value == "in_progress") return ContentNodeState::InProgress;
    if (value == "completed") return ContentNodeState::Completed;
    if (value == "skipped") return ContentNodeState::Skipped;
    return std::nullopt;
}

struct ContentProgress
{
    Uid nodeId;
    Uid userId;
    ContentNodeState state = ContentNodeState::NotStarted;
    double progress = 0.0;              // 0..1；completed=1，not_started=0
    std::string updatedAt;
    std::optional<std::string> sourceEventUid;   // 指向追加式进度事件

    bool isValid() const
    {
        return !nodeId.empty() && !userId.empty() && progress >= 0.0 && progress <= 1.0
               && !updatedAt.empty();
    }
};

// 覆盖进度行：仓储返回节点当前状态，用例层按口径计算覆盖率。
struct ContentProgressRow
{
    Uid nodeId;
    double weight = 1.0;
    ContentNodeState state = ContentNodeState::NotStarted;
    double progress = 0.0;
};

struct CoverageSummary
{
    Uid targetUid;                      // 地图或目标
    bool perGoal = false;               // true = 按目标聚合
    int totalNodes = 0;
    int completedNodes = 0;
    int inProgressNodes = 0;
    int skippedNodes = 0;
    double totalWeight = 0.0;           // 口径分母：跳过节点不计入
    double coveredWeight = 0.0;         // completed 全权重 + in_progress 按比例
    double coverage = 0.0;              // 0..1
    std::string caliberText;            // 计算口径说明（如"按 9 个节点等权，跳过 1 个"）
};

} // namespace PersonOS::Domain
