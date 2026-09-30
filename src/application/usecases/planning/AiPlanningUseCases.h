#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/AiConfigStore.h"
#include "application/ports/AiPorts.h"
#include "application/ports/DomainManifestRepository.h"
#include "application/ports/GoalRepository.h"
#include "application/ports/KnowledgeRepository.h"
#include "application/ports/KnowledgeRetrievalPort.h"
#include "application/ports/MelRepository.h"
#include "application/ports/RouteRepository.h"
#include "application/ports/StateRepository.h"
#include "application/ports/UuidPort.h"
#include "application/usecases/advice/AdviceUseCases.h"   // DecisionStore
#include "application/usecases/domain/DomainRegistry.h"   // DomainConfig
#include "domain/ai/Ai.h"
#include "domain/foundation/Clock.h"
#include "domain/knowledge/Retrieval.h"

// AI 端到端规划管线（DR-015/016/017/027；architecture 4.3.3/4.4）
// 固定顺序：领域配置 → 状态上下文 → 知识检索与快照 → 知识支持判定 →
// AI 候选（持久化任务）→ 契约/硬约束校验 → 候选落库（用户确认后生效）。
// 纪律：AI 只能引用检索召回的来源；校验失败不写入正式数据；
// 无可用模型连接 → ExternalUnavailable（任务保持可重试，不伪造结果）。
namespace PersonOS::Application {

class AiPlanningUseCases
{
public:
    struct ProposalOutput
    {
        std::string aggregateUid;        // 生成的路线/MEL uid
        std::string decisionUid;         // 决策记录（可追溯依据）
        std::string knowledgeSnapshotUid;
        Domain::SourceMode sourceMode = Domain::SourceMode::Ungrounded;
        std::string userText;            // 面向用户的说明（AI 输出）
        std::vector<std::string> warnings;
        std::string jobUid;              // 底层 AI 任务 uid(物理删除回答时联动)
    };

    AiPlanningUseCases(AiGatewayPort &gateway, AiConfigStore &configs,
                       KnowledgeRetrievalPort &retrieval, KnowledgeRepository &knowledge,
                       GoalRepository &goals,
                       MelRepository &mels, RouteRepository &routes,
                       StateRepository &states,
                       DomainManifestRepository &manifests, DecisionStore &decisions,
                       UuidPort &uids, const Domain::Clock &clock);

    // AI 生成路线候选（候选 → 用户确认后才成为当前路线）
    Result<ProposalOutput, ApplicationError> generateRouteProposal(
        const Domain::Uid &userId, const Domain::Uid &goalUid,
        const std::string &userGuidance = {});

    // AI 生成阶段详情候选（回答"如何真正完成这一阶段"；阶段必须属于已确认
    // 路线；详情整版候选 → 用户确认；资料建议只能引用检索召回条目）
    Result<ProposalOutput, ApplicationError> generateStageDetail(
        const Domain::Uid &userId, const Domain::Uid &stageUid,
        const std::string &userGuidance = {});

    // AI 生成 MEL 候选（周期取自领域清单参数；候选 → 用户确认激活）
    Result<ProposalOutput, ApplicationError> generateMelProposal(
        const Domain::Uid &userId, const Domain::Uid &goalUid,
        const std::optional<Domain::Uid> &routeVersionUid);

    // 逐任务方法建议：检索方法库 → AI 从召回候选中选择并给出理由 →
    // 绑定落库（不得凭空引用未召回的方法）
    Result<int, ApplicationError> generateMethodSuggestions(const Domain::Uid &userId,
                                                            const Domain::Uid &melUid);

    // 用户问题咨询（知识校准 + 通用问答契约；结果保存为决策记录）
    Result<ProposalOutput, ApplicationError> askAdvisor(const Domain::Uid &userId,
                                                        const std::string &question);

    // 用户在界面确认/拒绝候选后更新决策记录状态
    Result<void, ApplicationError> markDecision(const std::string &decisionUid,
                                                const std::string &userStatus,
                                                const std::optional<std::string> &selectedJson);

private:
    // 领域配置 + 状态上下文 + 知识检索 + 支持判定（管线前置四步）
    struct CalibrationContext
    {
        DomainConfig config;
        std::string stateContextJson;
        std::string knowledgeVersionsJson;
        std::string knowledgeSummary;
        std::string snapshotUid;
        Domain::SourceMode sourceMode;
        bool hasMaterialConflict = false;
        std::vector<Domain::RetrievalHit> hits;   // 检索命中（阶段详情资料候选集）
    };
    Result<CalibrationContext, ApplicationError> calibrate(
        const std::string &purpose, const std::string &queryText,
        const std::string &filtersJson, int keySubQuestions);

    Result<ProposalOutput, ApplicationError> runJob(
        const std::string &jobType, const std::string &contractType,
        const std::string &payloadJson, const CalibrationContext &calibration);

    AiGatewayPort &m_gateway;
    AiConfigStore &m_configs;
    KnowledgeRetrievalPort &m_retrieval;
    KnowledgeRepository &m_knowledge;
    GoalRepository &m_goals;
    MelRepository &m_mels;
    RouteRepository &m_routes;
    StateRepository &m_states;
    DomainManifestRepository &m_manifests;
    DecisionStore &m_decisions;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
