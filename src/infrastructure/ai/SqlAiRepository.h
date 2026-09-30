#pragma once

#include <QSqlDatabase>

#include <optional>
#include <vector>

#include "application/ports/GoalRepository.h"   // SaveResult
#include "application/ports/AiConfigStore.h"
#include "application/usecases/advice/AdviceUseCases.h"   // DecisionStore
#include "domain/ai/Ai.h"
#include "domain/foundation/Clock.h"
#include "domain/foundation/Uid.h"

// AI 基础设施仓储（ai_provider_configs_v6 / ai_jobs_v6 / ai_calls_v6 /
// ai_context_items_v6 / decision_records_v6）
namespace PersonOS::Infrastructure {

class SqlAiRepository : public Application::DecisionStore,
                        public Application::AiConfigStore
{
public:
    explicit SqlAiRepository(QSqlDatabase database, const Domain::Clock &clock);

    // 连接配置（revision 守卫；禁用而非物理删除）
    std::optional<Domain::AiProviderConfig> findConfig(const Domain::Uid &uid) override;
    std::optional<Domain::AiProviderConfig> findEnabledConfig(const Domain::Uid &uid);
    // 设置页/助手页只读投影
    std::vector<Domain::AiProviderConfig> listConfigs();
    std::optional<Domain::AiProviderConfig> findFirstEnabledConfig() override;
    Application::SaveResult insertConfig(const Domain::AiProviderConfig &config);
    Application::SaveResult updateConfig(const Domain::AiProviderConfig &config,
                                         int expectedRevision);
    // 删除连接配置连同其 v7 测试事实（同一事务；调用方负责先停用与清理凭据）
    Application::SaveResult deleteConfig(const Domain::Uid &uid);
    // 设为默认(单一默认:事务内先清后设,部分唯一索引兜底)
    Application::SaveResult setDefaultConfig(const Domain::Uid &uid);
    Application::SaveResult insertConnectionTest(const Domain::AiConnectionTest &test);
    std::optional<Domain::AiConnectionTest> latestConnectionTest(
        const Domain::Uid &configUid);

    // 任务（幂等键唯一）
    std::optional<Domain::AiJob> findJob(const Domain::Uid &uid);
    // AI 助手历史只读投影
    std::vector<Domain::AiJob> listJobs(int limit);
    Application::SaveResult insertJob(const Domain::AiJob &job);
    Application::SaveResult updateJob(const Domain::AiJob &job, int expectedRevision);
    bool existsJobKey(const std::string &idempotencyKey);
    std::optional<Domain::AiJob> findJobByKey(const std::string &idempotencyKey);

    // 调用与上下文
    Application::SaveResult insertCall(const Domain::Uid &callUid,
                                       const Domain::Uid &jobUid,
                                       const Domain::Uid &configUid,
                                       const std::string &requestHash,
                                       const std::string &contextHash,
                                       const std::string &responseHash, qint64 latencyMs,
                                       const std::string &status);
    Application::SaveResult insertContextItem(const Domain::Uid &callUid,
                                              const std::string &contextType,
                                              const std::string &contextUid,
                                              const std::string &versionUid, int rank,
                                              const std::string &reason);

    // 决策记录
    Application::SaveResult insertDecision(const Domain::DecisionRecord &decision) override;
    std::vector<Domain::DecisionRecord> listDecisions(const std::string &decisionType,
                                                      int limit) override;
    std::optional<Domain::DecisionRecord> findDecision(const Domain::Uid &uid);
    Application::SaveResult updateDecisionStatus(const Domain::Uid &uid,
                                                 const std::string &userStatus,
                                                 const std::optional<std::string> &selectedJson);
    // 物理删除决策记录及其关联 AI 任务/调用(2026-09-29 用户决策:
    // 咨询回答可物理删除;ai_context_items 经 ai_calls 级联;审计事件保留)
    Application::SaveResult deleteDecision(const Domain::Uid &uid);

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;
    std::optional<qint64> resolvePk(const char *sql, const std::string &uid) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
