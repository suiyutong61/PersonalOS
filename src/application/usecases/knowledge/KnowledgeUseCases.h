#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/EmbeddingPort.h"
#include "application/ports/EmbeddingVectorStore.h"
#include "application/ports/KnowledgeRepository.h"
#include "application/ports/SearchIndexPort.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/knowledge/Knowledge.h"

// 知识用例（DD-001 §5.2 ImportKnowledge/DeriveMethods 基础；requirements R4.4.1/R4.7）
// 纪律：
// - 版本不可变：新增版本不覆盖；当前版本指针通过条目更新切换（revision 守卫）；
// - 论文与方法不可物理删除（DR-012）：停用/归档以状态表达；
// - 每个正式版本可追溯来源（ImportInput.source 必填，generated 除外）。
namespace PersonOS::Application {

class KnowledgeUseCases
{
public:
    struct ImportInput
    {
        Domain::LibraryType libraryType;
        std::string title;
        std::string domainCode;
        std::string referenceCode;   // 编号(如 7.41 / 7.41-M1),可空
        std::string summary;
        std::string claimsJson = "[]";
        std::string applicabilityJson = "{}";
        std::string limitationsJson = "[]";
        std::string warningJson = "{}";
        std::string contentHash;        // 必填：内容指纹
        std::string language = "zh";
        std::optional<std::string> extractionModel;
        std::optional<std::string> extractionPromptVersion;
        std::string createdBy = "user";
        Domain::KnowledgeStatus initialStatus = Domain::KnowledgeStatus::Active;
        std::optional<Domain::SourceRecord> source;   // 非 generated 导入必填
        std::optional<Domain::PaperDetail> paper;
        std::optional<Domain::MethodDetail> method;
        std::optional<Domain::TipDetail> tip;
        std::optional<Domain::PlanDetail> plan;
        std::vector<Domain::MethodStep> methodSteps;
    };

    struct ImportOutput
    {
        Domain::KnowledgeItem item;
        std::string versionUid;
        std::optional<std::string> sourceUid;
    };

    KnowledgeUseCases(KnowledgeRepository &repo, SearchIndexPort &searchIndex, UuidPort &uids,
                       const Domain::Clock &clock, EmbeddingPort *embeddings = nullptr,
                       EmbeddingVectorStore *vectorStore = nullptr);

    // 导入新知识：条目 + 首个版本 + 来源 + 类型详情一次完成
    Result<ImportOutput, ApplicationError> importKnowledge(const ImportInput &input);

    // 追加新版本（不可变版本快照）；isActive=true 时切换当前版本指针
    struct VersionInput
    {
        std::string summary;
        std::string claimsJson = "[]";
        std::string applicabilityJson = "{}";
        std::string limitationsJson = "[]";
        std::string warningJson = "{}";
        std::string contentHash;
        std::string createdBy = "user";
        std::optional<std::string> extractionModel;
        std::optional<std::string> extractionPromptVersion;
        bool isActive = false;
    };
    Result<Domain::KnowledgeVersion, ApplicationError> addVersion(
        const Domain::Uid &itemUid, int expectedRevision, const VersionInput &input);

    // 停用/归档（状态表达，绝不物理删除）
    Result<Domain::KnowledgeItem, ApplicationError> deprecate(const Domain::Uid &itemUid,
                                                              int expectedRevision,
                                                              Domain::KnowledgeStatus status);

    // 物理删除(2026-09-29 用户决策:仅限 AI 生成候选;资格校验在
    // PaperAnalysisUseCases::purgeGeneratedCandidates)。级联删除
    // 条目/版本/类型详情/步骤/关系并同步移除 FTS 索引行。
    Result<int, ApplicationError> purgeItem(const Domain::Uid &itemUid);

    // 建立有类型知识关系
    Result<Domain::KnowledgeRelation, ApplicationError> relate(
        const Domain::KnowledgeRelation &relation);

    // 追加证据关系（版本 ↔ 原文片段）
    Result<Domain::EvidenceLink, ApplicationError> linkEvidence(
        const Domain::EvidenceLink &link);

private:
    // 索引侧向量（可选）：两端齐全且 modelId 非空时才生成条目向量；
    // 推理失败静默跳过（DR-013：向量不可用不阻断导入/检索）
    std::optional<std::vector<float>> embedForIndex(const std::string &title,
                                                    const std::string &summary) const;
    // 事务内 upsert（失败仅警告，不阻断导入）
    void tryUpsertVector(const std::string &ownerType, const std::string &ownerUid,
                         const std::string &contentHash,
                         const std::optional<std::vector<float>> &vector) const;

    KnowledgeRepository &m_repo;
    SearchIndexPort &m_searchIndex;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
    EmbeddingPort *m_embeddings = nullptr;
    EmbeddingVectorStore *m_vectorStore = nullptr;
};

} // namespace PersonOS::Application
