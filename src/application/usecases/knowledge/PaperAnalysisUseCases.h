#pragma once

#include <string>
#include <vector>
#include <optional>

#include "application/foundation/Result.h"
#include "application/ports/AiPorts.h"
#include "application/ports/KnowledgeRepository.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "domain/foundation/Uid.h"

namespace PersonOS::Application {

class PaperAnalysisUseCases
{
public:
    struct AnalyzeInput
    {
        Domain::Uid paperItemUid;
        Domain::Uid sourceUid;
        std::optional<Domain::Uid> sourceAssetUid;
        Domain::Uid providerConfigUid;
        std::string title;
        std::string extractedText;
        std::string analysisContentHash;
        std::string modelVersion;
        std::string promptVersion = "paper-analysis-v2";
        std::string idempotencyKey;
        int maxChunkCharacters = 60000;
        int chunkOverlapCharacters = 2000;
    };

    struct AnalyzeOutput
    {
        Domain::KnowledgeVersion paperVersion;
        bool reusedExisting = false;   // 同内容指纹已有草稿,直接复用(幂等重跑)
        std::vector<Domain::KnowledgeItem> candidateMethods;
        std::vector<Domain::KnowledgeItem> candidateTips;
        int evidenceFragmentCount = 0;
        int analyzedChunkCount = 0;
        std::string rawAnalysisJson;
    };

    PaperAnalysisUseCases(AiGatewayPort &gateway, KnowledgeRepository &repository,
                          KnowledgeUseCases &knowledge, UuidPort &uids,
                          const Domain::Clock &clock);

    Result<AnalyzeOutput, ApplicationError> analyze(const AnalyzeInput &input);

    struct PurgeInput
    {
        Domain::Uid paperItemUid;
        std::vector<Domain::Uid> candidateItemUids;
    };

    struct PurgeOutput
    {
        int purgedCount = 0;
    };

    // 物理删除 AI 生成候选(2026-09-29 用户决策,需求 R4.4 变更记录):
    // 仅限 candidate+createdBy=generated 且与论文有 derived_from 的条目,
    // 同一事务内级联删除并同步 FTS;论文、用户条目与历史版本不受影响。
    Result<PurgeOutput, ApplicationError> purgeGeneratedCandidates(const PurgeInput &input);

    struct RetractInput
    {
        Domain::Uid paperItemUid;
        Domain::Uid analysisVersionUid;
        std::vector<Domain::Uid> candidateItemUids;
    };
    struct RetractOutput
    {
        int archivedCandidateCount = 0;
        Domain::Uid retainedAnalysisVersionUid;
    };

    // 非破坏性撤销：分析草稿永久保留供审计，只归档本次生成的候选条目。
    Result<RetractOutput, ApplicationError> retract(const RetractInput &input);

private:
    AiGatewayPort &m_gateway;
    KnowledgeRepository &m_repository;
    KnowledgeUseCases &m_knowledge;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
