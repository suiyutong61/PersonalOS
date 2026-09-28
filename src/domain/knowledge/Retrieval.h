#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 检索与知识快照（DD-001 §7；DR-007/013/014；数据库设计 §6）
// 检索结果必须保留知识 ID、版本、来源、各路分数与选择理由（可追溯）；
// 快照固化所用知识版本，后续复现不受知识库漂移影响。
namespace PersonOS::Domain {

enum class RetrievalStatus { Running, Completed, Failed, Cancelled };

inline std::string toString(RetrievalStatus s)
{
    switch (s) {
    case RetrievalStatus::Running: return "running";
    case RetrievalStatus::Completed: return "completed";
    case RetrievalStatus::Failed: return "failed";
    case RetrievalStatus::Cancelled: return "cancelled";
    }
    return "running";
}

struct RetrievalRun
{
    Uid uid;
    std::string purpose;
    std::string queryText;
    std::string filtersJson = "{}";
    std::string strategyVersion = "1";
    RetrievalStatus status = RetrievalStatus::Running;
    std::string startedAt;
    std::optional<std::string> completedAt;
    std::optional<std::string> knowledgeSnapshotUid;

    bool isValid() const
    {
        return !uid.empty() && !purpose.empty() && !queryText.empty()
               && !startedAt.empty();
    }
};

struct RetrievalHit
{
    std::string ownerType;            // paper/method/tip/plan
    std::string ownerUid;
    std::optional<double> lexicalScore;
    std::optional<double> vectorScore;   // 向量召回在 IMP-006 接入
    double finalScore = 0.0;
    int rank = 1;
    std::string reasonJson = "{}";    // 选择理由（保留检索依据）
};

// 知识支持程度（DR-027：三档 + 冲突独立标志）
enum class KnowledgeSupportLevel { Grounded, PartiallyGrounded, Ungrounded };

inline std::string toString(KnowledgeSupportLevel level)
{
    switch (level) {
    case KnowledgeSupportLevel::Grounded: return "grounded";
    case KnowledgeSupportLevel::PartiallyGrounded: return "partially_grounded";
    case KnowledgeSupportLevel::Ungrounded: return "ungrounded";
    }
    return "ungrounded";
}

struct KnowledgeSnapshot
{
    Uid uid;
    std::string createdAt;
    std::string purpose;
    std::string manifestVersionsJson = "[]";
    std::string knowledgeVersionsJson = "[]";   // 固化：[(owner_type,owner_uid,version_uid),...]
    std::optional<std::string> stateSnapshotUid;
    std::optional<std::string> retrievalRunUid;
    std::string contentHash;          // UNIQUE：同内容幂等
};

} // namespace PersonOS::Domain
