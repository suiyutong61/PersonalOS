#include "presentation/viewmodels/AiServices.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "application/usecases/domain/DomainRegistry.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/HttpEmbeddingPort.h"
#include "infrastructure/ai/OpenAiCompatibleProvider.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/ai/WindowsCredentialStore.h"
#include "infrastructure/embedding/LocalEmbeddingProvider.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"
#include "infrastructure/persistence/SqlDomainManifestRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

namespace PersonOS::Presentation {

std::unique_ptr<PlanningPipeline> buildPlanningPipeline(QSqlDatabase database)
{
    auto pipeline = std::make_unique<PlanningPipeline>();
    pipeline->clock = std::make_unique<Infrastructure::QtSystemClock>();
    pipeline->uids = std::make_unique<Infrastructure::QtUidGenerator>();
    pipeline->credentials = std::make_unique<Infrastructure::WindowsCredentialStore>();
    pipeline->provider = std::make_unique<Infrastructure::OpenAiCompatibleProvider>(
        *pipeline->credentials);
    pipeline->aiRepo = std::make_unique<Infrastructure::SqlAiRepository>(database,
                                                                         *pipeline->clock);
    pipeline->gateway = std::make_unique<Infrastructure::AiGateway>(
        *pipeline->aiRepo, *pipeline->provider, *pipeline->uids, *pipeline->clock);
    pipeline->retrieval = std::make_unique<Infrastructure::SqlKnowledgeRetrieval>(
        database, *pipeline->clock);
    // 向量通道（可选）：本地嵌入模型优先（零 API 成本）；否则启用的模型
    // 连接声明 embedding 能力时经 HTTP 注入；两者皆无时检索降级为
    // 结构化+全文（DR-013）
    if (Infrastructure::LocalEmbeddingProvider::filesPresent()) {
        pipeline->retrieval->setEmbeddingPort(&Infrastructure::sharedLocalEmbedding());
    } else {
        const auto config = pipeline->aiRepo->findFirstEnabledConfig();
        if (config) {
            const QJsonDocument capabilities = QJsonDocument::fromJson(
                QByteArray::fromStdString(config->capabilitiesJson));
            if (capabilities.object().value(QStringLiteral("embedding")).toBool(false)) {
                pipeline->embeddings =
                    std::make_unique<Infrastructure::HttpEmbeddingPort>(
                        *pipeline->aiRepo, *pipeline->credentials);
                pipeline->retrieval->setEmbeddingPort(pipeline->embeddings.get());
            }
        }
    }
    pipeline->knowledgeRepo = std::make_unique<Infrastructure::SqlKnowledgeRepository>(
        database, *pipeline->clock);
    pipeline->goalsRepo = std::make_unique<Infrastructure::SqlGoalRepository>(database,
                                                                              *pipeline->clock);
    pipeline->melsRepo = std::make_unique<Infrastructure::SqlMelRepository>(database,
                                                                            *pipeline->clock);
    pipeline->routesRepo = std::make_unique<Infrastructure::SqlRouteRepository>(
        database, *pipeline->clock);
    pipeline->statesRepo = std::make_unique<Infrastructure::SqlStateRepository>(
        database, *pipeline->clock);
    pipeline->manifestsRepo =
        std::make_unique<Infrastructure::SqlDomainManifestRepository>(database);
    pipeline->useCases = std::make_unique<Application::AiPlanningUseCases>(
        *pipeline->gateway, *pipeline->aiRepo, *pipeline->retrieval,
        *pipeline->knowledgeRepo, *pipeline->goalsRepo, *pipeline->melsRepo,
        *pipeline->routesRepo, *pipeline->statesRepo, *pipeline->manifestsRepo,
        *pipeline->aiRepo, *pipeline->uids, *pipeline->clock);
    return pipeline;
}

namespace {

std::optional<double> parameterDefault(QSqlDatabase database, const std::string &id)
{
    Infrastructure::SqlDomainManifestRepository manifests(database);
    Application::DomainRegistry registry(manifests);
    const auto config = registry.load("learning");
    if (!config)
        return std::nullopt;
    const auto parameter = config.value().parameter(id);
    if (!parameter)
        return std::nullopt;
    return parameter->defaultValue;
}

} // namespace

std::optional<Application::DetectionUseCases::DetectionConfig> detectionConfig(
    QSqlDatabase database)
{
    const auto lag = parameterDefault(database, "detection_progress_lag_tolerance");
    const auto inactivity = parameterDefault(database, "detection_inactivity_minutes");
    const auto effort = parameterDefault(database, "detection_effort_ratio_threshold");
    const auto window =
        parameterDefault(database, "detection_weak_assessment_window_minutes");
    if (!lag || !inactivity || !effort || !window)
        return std::nullopt;

    Application::DetectionUseCases::DetectionConfig config;
    config.progressLagTolerance = *lag;
    config.inactivityMinutes = static_cast<int>(*inactivity);
    config.effortRatioThreshold = *effort;
    config.weakAssessmentWindowMinutes = static_cast<int>(*window);
    config.basisSource = "domain:learning:manifest:v1";
    return config;
}

std::optional<Application::AdviceUseCases::AdviceConfig> adviceConfig(QSqlDatabase database)
{
    const auto quiet = parameterDefault(database, "advice_quiet_window_minutes");
    const auto maximum = parameterDefault(database, "advice_max_active");
    if (!quiet || !maximum)
        return std::nullopt;

    Application::AdviceUseCases::AdviceConfig config;
    config.quietWindowMinutes = static_cast<int>(*quiet);
    config.maxActiveAdvice = static_cast<int>(*maximum);
    config.basisSource = "domain:learning:manifest:v1";
    return config;
}

} // namespace PersonOS::Presentation
