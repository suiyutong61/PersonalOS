#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 复盘聚合（DD-001 §3；数据库设计 §4.2 reviews_v4）
// 复盘必须区分执行完成与能力掌握（DR-022）；结论经用户确认后校准下一周期。
namespace PersonOS::Domain {

enum class ReviewStatus { Draft, Collecting, Assessing, Decision, Confirmed, Closed };

inline std::string toString(ReviewStatus s)
{
    switch (s) {
    case ReviewStatus::Draft: return "draft";
    case ReviewStatus::Collecting: return "collecting";
    case ReviewStatus::Assessing: return "assessing";
    case ReviewStatus::Decision: return "decision";
    case ReviewStatus::Confirmed: return "confirmed";
    case ReviewStatus::Closed: return "closed";
    }
    return "draft";
}

inline std::optional<ReviewStatus> reviewStatusFrom(std::string_view value)
{
    if (value == "draft") return ReviewStatus::Draft;
    if (value == "collecting") return ReviewStatus::Collecting;
    if (value == "assessing") return ReviewStatus::Assessing;
    if (value == "decision") return ReviewStatus::Decision;
    if (value == "confirmed") return ReviewStatus::Confirmed;
    if (value == "closed") return ReviewStatus::Closed;
    return std::nullopt;
}

struct Review
{
    Uid uid;
    Uid melId;                        // 每个 MEL 至多一个复盘（UNIQUE）
    ReviewStatus status = ReviewStatus::Draft;
    std::string startedAt;
    std::optional<std::string> completedAt;
    std::string summary;              // 完成比例、耗时偏差、验收结果、主要问题
    std::string userComment;          // 用户说明
    std::string nextAction;           // 下一动作：新 MEL / 暂停 / 改路线 / 结束目标
    std::optional<std::string> knowledgeSnapshotUid;
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const { return !uid.empty() && !melId.empty() && !startedAt.empty(); }
};

} // namespace PersonOS::Domain
