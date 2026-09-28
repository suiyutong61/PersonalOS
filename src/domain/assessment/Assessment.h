#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 能力验收聚合（DD-001 §3；数据库设计 §4.3；DR-022 三档结果）
// 不变量：进度完成 ≠ 掌握（DR-019）；三档结果只描述本次检测表现，
// 不宣称永久掌握；100% 执行完成不自动产生掌握结论。
namespace PersonOS::Domain {

enum class AssessmentStatus { Draft, Ready, InProgress, Scored, Cancelled };

inline std::string toString(AssessmentStatus s)
{
    switch (s) {
    case AssessmentStatus::Draft: return "draft";
    case AssessmentStatus::Ready: return "ready";
    case AssessmentStatus::InProgress: return "in_progress";
    case AssessmentStatus::Scored: return "scored";
    case AssessmentStatus::Cancelled: return "cancelled";
    }
    return "draft";
}

inline std::optional<AssessmentStatus> assessmentStatusFrom(std::string_view value)
{
    if (value == "draft") return AssessmentStatus::Draft;
    if (value == "ready") return AssessmentStatus::Ready;
    if (value == "in_progress") return AssessmentStatus::InProgress;
    if (value == "scored") return AssessmentStatus::Scored;
    if (value == "cancelled") return AssessmentStatus::Cancelled;
    return std::nullopt;
}

// 三档掌握（DR-022）：无需提示 / 提示后 / 提示后仍不能；不适用
enum class Mastery { Fluent, Prompted, NotRecalled, NotApplicable };

inline std::string toString(Mastery m)
{
    switch (m) {
    case Mastery::Fluent: return "fluent";
    case Mastery::Prompted: return "prompted";
    case Mastery::NotRecalled: return "not_recalled";
    case Mastery::NotApplicable: return "not_applicable";
    }
    return "not_applicable";
}

inline std::optional<Mastery> masteryFrom(std::string_view value)
{
    if (value == "fluent") return Mastery::Fluent;
    if (value == "prompted") return Mastery::Prompted;
    if (value == "not_recalled") return Mastery::NotRecalled;
    if (value == "not_applicable") return Mastery::NotApplicable;
    return std::nullopt;
}

struct AssessmentItem
{
    Uid uid;
    Uid assessmentId;
    std::string itemType;             // recall/explanation/exercise/operation...
    std::string prompt;
    std::string referenceAnswer;
    std::string rubricJson = "{}";
    std::string sourceRefsJson = "[]";   // 题目来源（考试名称/年份/是否 AI 生成）
    int sequenceNo = 0;
    std::optional<double> difficulty;    // 0..1
    int revision = 1;

    bool isValid() const { return !uid.empty() && !prompt.empty(); }
};

struct Assessment
{
    Uid uid;
    Uid userId;
    Uid goalId;                       // 必填
    std::optional<Uid> melId;
    std::string assessmentType;       // recall/explanation/mind_map/exercise/project/...
    AssessmentStatus status = AssessmentStatus::Draft;
    std::string scopeJson = "{}";     // 检查范围（依据本轮已确认内容节点）
    std::string rubricJson = "{}";    // 判定规则与提示标准
    std::string generatedBy;          // user / ai
    std::optional<std::string> knowledgeSnapshotUid;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !goalId.empty()
               && !assessmentType.empty();
    }
};

struct AssessmentAttempt
{
    Uid uid;
    Uid assessmentId;
    std::string startedAt;
    std::optional<std::string> submittedAt;
    std::string answerJson = "{}";    // 题目→作答
    std::optional<std::string> evidenceAssetUid;
    std::optional<double> selfRating;
    std::string idempotencyKey;       // UNIQUE

    bool isValid() const
    {
        return !uid.empty() && !assessmentId.empty() && !startedAt.empty()
               && !idempotencyKey.empty();
    }
};

struct AssessmentResult
{
    Uid uid;
    Uid attemptId;
    std::optional<Uid> itemId;        // 空 = 整体结果
    Mastery mastery = Mastery::NotApplicable;
    std::optional<double> score;
    std::string feedback;
    std::string scorer;               // user / ai / rule / external
    double confidence = 1.0;
    std::string createdAt;
    bool confirmedByUser = false;

    bool isValid() const
    {
        return !uid.empty() && !attemptId.empty() && !scorer.empty()
               && confidence >= 0.0 && confidence <= 1.0;
    }
};

} // namespace PersonOS::Domain
