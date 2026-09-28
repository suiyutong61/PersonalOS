#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 内容摄入（DD-001 §8.1；DR-023；数据库设计 §6 content_import_jobs_v6）
// 数据库约束：status 只取队列态七值；管道阶段存于 stage 字段（自由文本）。
// 管道：fetching → quarantined → extracting → normalizing → classifying →
//       deduplicating → deriving → validating → awaiting_confirmation → committed
namespace PersonOS::Domain {

enum class IngestStatus {
    Queued,
    Running,
    AwaitingConfirmation,
    Committed,
    FailedRetryable,
    FailedTerminal,
    Cancelled,
};

inline std::string toString(IngestStatus s)
{
    switch (s) {
    case IngestStatus::Queued: return "queued";
    case IngestStatus::Running: return "running";
    case IngestStatus::AwaitingConfirmation: return "awaiting_confirmation";
    case IngestStatus::Committed: return "committed";
    case IngestStatus::FailedRetryable: return "failed_retryable";
    case IngestStatus::FailedTerminal: return "failed_terminal";
    case IngestStatus::Cancelled: return "cancelled";
    }
    return "queued";
}

inline std::optional<IngestStatus> ingestStatusFrom(std::string_view value)
{
    for (int i = 0; i <= static_cast<int>(IngestStatus::Cancelled); ++i) {
        const auto status = static_cast<IngestStatus>(i);
        if (toString(status) == value)
            return status;
    }
    return std::nullopt;
}

// 管道阶段（stage 字段取值）
namespace IngestStage {
inline constexpr const char *Fetching = "fetching";
inline constexpr const char *Quarantined = "quarantined";
inline constexpr const char *Extracting = "extracting";
inline constexpr const char *Normalizing = "normalizing";
inline constexpr const char *Classifying = "classifying";
inline constexpr const char *Deduplicating = "deduplicating";
inline constexpr const char *Deriving = "deriving";
inline constexpr const char *Validating = "validating";
inline constexpr const char *AwaitingConfirmation = "awaiting_confirmation";
inline constexpr const char *Committed = "committed";

// 阶段推进顺序（Running 状态内）
inline constexpr const char *const Pipeline[] = {
    Fetching,  Quarantined, Extracting, Normalizing, Classifying,
    Deduplicating, Deriving, Validating,
};

inline const char *nextStage(const std::string &current)
{
    for (std::size_t i = 0; i < sizeof(Pipeline) / sizeof(Pipeline[0]); ++i) {
        if (current == Pipeline[i])
            return i + 1 < sizeof(Pipeline) / sizeof(Pipeline[0]) ? Pipeline[i + 1]
                                                                  : nullptr;
    }
    return nullptr;
}
} // namespace IngestStage

struct ContentImportJob
{
    Uid uid;
    std::string sourceType;           // url / text / file
    std::string sourceUri;
    IngestStatus status = IngestStatus::Queued;
    std::string stage = "queued";     // 管道阶段（Running 状态内使用）
    std::optional<std::string> errorCode;
    std::optional<std::string> errorDetail;
    std::string idempotencyKey;       // UNIQUE（同来源幂等）
    std::string requestedBy;          // user / system
    std::optional<std::string> startedAt;
    std::optional<std::string> completedAt;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !sourceType.empty() && !sourceUri.empty()
               && !idempotencyKey.empty();
    }
};

} // namespace PersonOS::Domain
