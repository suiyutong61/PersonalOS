#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 五库共享骨架（DD-001 §7；数据库设计 §5；DR-006/008/029）
// 不变量：
// - 知识正文为不可变版本快照（只追加新版本，不覆盖；DR-011/030）；
// - 论文与方法不可物理删除（never_delete=1，停用/归档替代；DR-012）；
// - 关系有类型（supports/qualifies/contradicts/...），禁止自关系；
// - 每个正式版本可追溯到来源与证据片段（DR-004）。
namespace PersonOS::Domain {

enum class LibraryType { Paper, Plan, State, Method, Tip };

inline std::string toString(LibraryType t)
{
    switch (t) {
    case LibraryType::Paper: return "paper";
    case LibraryType::Plan: return "plan";
    case LibraryType::State: return "state";
    case LibraryType::Method: return "method";
    case LibraryType::Tip: return "tip";
    }
    return "paper";
}

inline std::optional<LibraryType> libraryTypeFrom(std::string_view value)
{
    if (value == "paper") return LibraryType::Paper;
    if (value == "plan") return LibraryType::Plan;
    if (value == "state") return LibraryType::State;
    if (value == "method") return LibraryType::Method;
    if (value == "tip") return LibraryType::Tip;
    return std::nullopt;
}

enum class KnowledgeStatus { Draft, Candidate, Active, Warned, Superseded, Archived };

inline std::string toString(KnowledgeStatus s)
{
    switch (s) {
    case KnowledgeStatus::Draft: return "draft";
    case KnowledgeStatus::Candidate: return "candidate";
    case KnowledgeStatus::Active: return "active";
    case KnowledgeStatus::Warned: return "warned";
    case KnowledgeStatus::Superseded: return "superseded";
    case KnowledgeStatus::Archived: return "archived";
    }
    return "candidate";
}

inline std::optional<KnowledgeStatus> knowledgeStatusFrom(std::string_view value)
{
    if (value == "draft") return KnowledgeStatus::Draft;
    if (value == "candidate") return KnowledgeStatus::Candidate;
    if (value == "active") return KnowledgeStatus::Active;
    if (value == "warned") return KnowledgeStatus::Warned;
    if (value == "superseded") return KnowledgeStatus::Superseded;
    if (value == "archived") return KnowledgeStatus::Archived;
    return std::nullopt;
}

struct KnowledgeItem
{
    Uid uid;
    LibraryType libraryType = LibraryType::Paper;
    std::string title;
    std::string domainCode;           // 关联分类方向（learning 等）
    KnowledgeStatus status = KnowledgeStatus::Candidate;
    std::optional<std::string> currentVersionUid;
    std::string ownerScope = "system";   // system / user
    std::string createdBy;            // user / ai / system / developer
    bool neverDelete = false;         // 论文与方法强制 true
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !title.empty() && !domainCode.empty();
    }
};

struct KnowledgeVersion
{
    Uid uid;
    Uid itemUid;
    int versionNo = 1;
    KnowledgeStatus status = KnowledgeStatus::Candidate;
    std::string summary;
    std::string claimsJson = "[]";
    std::string applicabilityJson = "{}";
    std::string limitationsJson = "[]";
    std::string warningJson = "{}";
    std::string contentHash;          // 内容指纹（不可变）
    std::string language = "zh";
    std::optional<std::string> extractionModel;
    std::optional<std::string> extractionPromptVersion;
    std::string validFrom;
    std::optional<std::string> validTo;
    std::string createdBy;

    bool isValid() const
    {
        return !uid.empty() && !itemUid.empty() && versionNo > 0 && !summary.empty()
               && !contentHash.empty() && !validFrom.empty();
    }
};

struct SourceRecord
{
    Uid uid;
    std::string sourceType;           // file / url / doi / manual / conversation / generated
    std::optional<std::string> canonicalUri;
    std::string title;
    std::string publisher;
    std::optional<std::string> publishedAt;
    std::string accessedAt;
    std::string metadataJson = "{}";
    std::string contentHash;
    std::string trustTier;            // 信任分级（如 vetted/unverified）
    int revision = 1;

    bool isValid() const { return !uid.empty() && !title.empty() && !contentHash.empty(); }
};

struct EvidenceFragment
{
    Uid uid;
    Uid sourceUid;
    std::optional<std::string> assetUid;
    std::string locatorJson = "{}";   // 页码/章节/时间戳定位
    std::string text;                 // 原文片段
    std::string fragmentHash;

    bool isValid() const { return !uid.empty() && !sourceUid.empty(); }
};

// 证据关系：知识版本 ↔ 证据片段（supports/qualifies/contradicts/derived_from）
struct EvidenceLink
{
    std::string knowledgeVersionUid;
    std::string fragmentUid;
    std::string relation = "supports";
    double strength = 1.0;
    std::string note;

    bool isValid() const
    {
        return !knowledgeVersionUid.empty() && !fragmentUid.empty()
               && strength >= 0.0 && strength <= 1.0;
    }
};

// 知识间关系（DR-010/012：合并/演进/冲突等）
struct KnowledgeRelation
{
    std::string fromItemUid;
    std::string toItemUid;
    std::string relation;             // derived_from/supports/qualifies/contradicts/
                                      // similar_to/supersedes/combined_with
    double confidence = 1.0;
    std::optional<std::string> decisionUid;

    bool isValid() const
    {
        return !fromItemUid.empty() && !toItemUid.empty() && fromItemUid != toItemUid
               && !relation.empty() && confidence >= 0.0 && confidence <= 1.0;
    }
};

// 类型专用详情
struct PaperDetail
{
    Uid itemUid;
    std::optional<std::string> doi;
    std::string studyType;
    std::string qualityJson = "{}";
    std::string populationJson = "{}";
    std::string publicationJson = "{}";
};

struct MethodDetail
{
    Uid itemUid;
    std::string methodType;
    std::string riskLevel;
    std::string evidenceGrade;
    bool trialRecommended = false;
    std::string measurementJson = "{}";
};

struct TipDetail
{
    Uid itemUid;
    std::string captureType;
    std::optional<std::string> originalUri;
    std::string verificationStatus;
    std::string riskLevel;
    std::string useScenarioJson = "{}";
};

struct PlanDetail
{
    Uid itemUid;
    std::string targetProfileJson = "{}";
    std::string entryConditionsJson = "{}";
    std::string exitConditionsJson = "{}";
};

struct MethodStep
{
    Uid methodVersionUid;
    int sequenceNo = 0;
    std::string instruction;
    std::optional<int> durationMin;
    std::string configJson = "{}";

    bool isValid() const { return !methodVersionUid.empty() && !instruction.empty(); }
};

} // namespace PersonOS::Domain
