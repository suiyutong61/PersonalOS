#pragma once

#include <QSqlDatabase>

#include <memory>
#include <optional>

#include "application/usecases/advice/AdviceUseCases.h"
#include "application/usecases/detection/DetectionUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/HttpEmbeddingPort.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/WindowsCredentialStore.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"
#include "infrastructure/persistence/SqlDomainManifestRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"
#include "infrastructure/persistence/SqlUnitOfWork.h"

// AI 服务装配（ViewModel 侧组合根）：真实管线 = Windows 凭据库 + OpenAI 兼容
// Provider + AiGateway + 混合检索 + 领域清单；测试替身由测试自行组装。
// 检测/建议参数一律从领域清单读取（阈值与依据不写死在代码里）。
namespace PersonOS::Presentation {

struct PlanningPipeline
{
    // 持有依赖的成员（顺序即析构顺序，useCases 最先销毁）
    std::unique_ptr<Application::AiPlanningUseCases> useCases;
    std::unique_ptr<Infrastructure::AiGateway> gateway;
    std::unique_ptr<Infrastructure::HttpEmbeddingPort> embeddings;   // 可选（能力匹配时）
    std::unique_ptr<Infrastructure::OpenAiCompatibleProvider> provider;
    std::unique_ptr<Infrastructure::WindowsCredentialStore> credentials;
    std::unique_ptr<Infrastructure::SqlAiRepository> aiRepo;
    std::unique_ptr<Infrastructure::SqlKnowledgeRetrieval> retrieval;
    std::unique_ptr<Infrastructure::SqlKnowledgeRepository> knowledgeRepo;
    std::unique_ptr<Infrastructure::SqlGoalRepository> goalsRepo;
    std::unique_ptr<Infrastructure::SqlMelRepository> melsRepo;
    std::unique_ptr<Infrastructure::SqlRouteRepository> routesRepo;
    std::unique_ptr<Infrastructure::SqlStateRepository> statesRepo;
    std::unique_ptr<Infrastructure::SqlDomainManifestRepository> manifestsRepo;
    std::unique_ptr<Infrastructure::SqlUnitOfWork> unitOfWork;
    std::unique_ptr<Infrastructure::QtSystemClock> clock;
    std::unique_ptr<Infrastructure::QtUidGenerator> uids;
};

// 构建完整规划管线（每调用独立实例；任务在后台线程执行时各自持数据库连接）
std::unique_ptr<PlanningPipeline> buildPlanningPipeline(QSqlDatabase database);

// 从领域清单解析 R2 检测配置（basisSource = 清单 provenance；解析失败 nullopt）
std::optional<Application::DetectionUseCases::DetectionConfig> detectionConfig(
    QSqlDatabase database);

// 从领域清单解析 R5 建议配置
std::optional<Application::AdviceUseCases::AdviceConfig> adviceConfig(QSqlDatabase database);

} // namespace PersonOS::Presentation
