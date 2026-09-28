#include "infrastructure/ai/HttpEmbeddingPort.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "infrastructure/ai/OpenAiCompatibleProvider.h"

namespace PersonOS::Infrastructure {

HttpEmbeddingPort::HttpEmbeddingPort(Application::AiConfigStore &configs,
                                     Application::CredentialStorePort &credentials)
    : m_configs(configs), m_credentials(credentials)
{}

Application::Result<std::vector<float>, Application::ApplicationError>
HttpEmbeddingPort::embed(const std::string &text)
{
    if (text.empty())
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "embedding text empty", {}, false});

    const auto config = m_configs.findFirstEnabledConfig();
    if (!config)
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable,
             "no enabled provider config for embedding", {}, true});

    // 能力声明检查：模型必须声明 embedding 能力（DR-025 能力路由）
    const QJsonDocument capabilities = QJsonDocument::fromJson(
        QByteArray::fromStdString(config->capabilitiesJson));
    const bool declaresEmbedding =
        capabilities.object().value(QStringLiteral("embedding")).toBool(false);
    if (!declaresEmbedding)
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            {Application::ErrorCode::ExternalUnavailable,
             "enabled provider does not declare embedding capability", {}, true});

    OpenAiCompatibleProvider provider(m_credentials);
    const auto vector = provider.embed(*config, text);
    if (!vector)
        return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
            vector.error());
    m_dimension = static_cast<int>(vector.value().size());
    return vector;
}

} // namespace PersonOS::Infrastructure
