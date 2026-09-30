#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// AI 网关领域对象（DD-001 §9；DR-015/016/025/027；数据库设计 §6）
namespace PersonOS::Domain {

struct AiProviderConfig
{
    Uid uid;
    std::string providerCode;
    std::string displayName;
    std::string endpoint;
    std::string model;
    std::string credentialRef;        // 凭据库引用（绝不含明文密钥）
    std::string capabilitiesJson = "{}";
    bool enabled = true;
    bool isDefault = false;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !providerCode.empty() && !displayName.empty()
               && !endpoint.empty() && !model.empty() && !credentialRef.empty();
    }
};

struct AiConnectionTest
{
    Uid uid;
    Uid providerConfigUid;
    std::string testedAt;
    std::string providerModel;
    bool authOk = false;
    bool structuredOk = false;
    bool embeddingRequired = false;
    bool embeddingOk = false;
    bool overallOk = false;
    std::string capabilitiesJson = "{}";
    std::string errorJson = "{}";
};

enum class AiJobStatus {
    Queued,
    Running,
    Completed,
    FailedRetryable,
    FailedTerminal,
    Cancelled,
};

inline std::string toString(AiJobStatus s)
{
    switch (s) {
    case AiJobStatus::Queued: return "queued";
    case AiJobStatus::Running: return "running";
    case AiJobStatus::Completed: return "completed";
    case AiJobStatus::FailedRetryable: return "failed_retryable";
    case AiJobStatus::FailedTerminal: return "failed_terminal";
    case AiJobStatus::Cancelled: return "cancelled";
    }
    return "queued";
}

struct AiJob
{
    Uid uid;
    std::string jobType;
    AiJobStatus status = AiJobStatus::Queued;
    int priority = 50;
    std::string requestJson;
    std::optional<std::string> resultJson;
    std::string schemaVersion;
    std::string idempotencyKey;       // UNIQUE
    int attemptCount = 0;
    int maxAttempts = 3;
    std::optional<std::string> nextAttemptAt;
    std::optional<std::string> errorJson;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !jobType.empty() && !schemaVersion.empty()
               && !idempotencyKey.empty() && maxAttempts > 0;
    }
};

// 知识来源模式（DR-027 三档，与支持程度一一对应）
enum class SourceMode { KnowledgeGrounded, PartiallyGrounded, Ungrounded };

inline std::string toString(SourceMode m)
{
    switch (m) {
    case SourceMode::KnowledgeGrounded: return "knowledge_grounded";
    case SourceMode::PartiallyGrounded: return "partially_grounded";
    case SourceMode::Ungrounded: return "ungrounded";
    }
    return "ungrounded";
}

inline std::optional<SourceMode> sourceModeFrom(std::string_view value)
{
    if (value == "knowledge_grounded") return SourceMode::KnowledgeGrounded;
    if (value == "partially_grounded") return SourceMode::PartiallyGrounded;
    if (value == "ungrounded") return SourceMode::Ungrounded;
    return std::nullopt;
}

enum class DecisionUserStatus { NotRequired, Pending, Accepted, Modified, Rejected };

inline std::string toString(DecisionUserStatus s)
{
    switch (s) {
    case DecisionUserStatus::NotRequired: return "not_required";
    case DecisionUserStatus::Pending: return "pending";
    case DecisionUserStatus::Accepted: return "accepted";
    case DecisionUserStatus::Modified: return "modified";
    case DecisionUserStatus::Rejected: return "rejected";
    }
    return "pending";
}

struct DecisionRecord
{
    Uid uid;
    std::string decisionType;
    std::string aggregateType;
    std::string aggregateUid;
    std::string inputSnapshotJson = "{}";
    std::string candidateJson = "{}";
    std::optional<std::string> selectedJson;
    std::string rationale;
    SourceMode sourceMode = SourceMode::Ungrounded;
    std::string warningJson = "{}";
    DecisionUserStatus userStatus = DecisionUserStatus::Pending;
    std::string createdAt;
    std::optional<std::string> confirmedAt;
    // 关联的 AI 任务 uid(2026-09-29 用户决策:咨询回答可物理删除,
    // 删除时连同任务/调用记录一起;可空=历史数据无关联)
    std::optional<std::string> jobUid;

    bool isValid() const
    {
        return !uid.empty() && !decisionType.empty() && !aggregateType.empty()
               && !aggregateUid.empty() && !rationale.empty();
    }
};

} // namespace PersonOS::Domain
