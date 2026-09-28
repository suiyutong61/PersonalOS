#include "infrastructure/ai/ModelConnectionTestService.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "application/foundation/ContractValidator.h"

namespace PersonOS::Infrastructure {

ModelConnectionTestService::ModelConnectionTestService(
    SqlAiRepository &repository, Application::AiProviderPort &textProvider,
    Application::AiEmbeddingProviderPort *embeddingProvider,
    Application::UuidPort &uids, const Domain::Clock &clock)
    : m_repository(repository), m_textProvider(textProvider),
      m_embeddingProvider(embeddingProvider), m_uids(uids), m_clock(clock)
{}

Application::Result<Domain::AiConnectionTest, Application::ApplicationError>
ModelConnectionTestService::test(const Domain::Uid &configUid)
{
    const auto config = m_repository.findConfig(configUid);
    if (!config)
        return Application::Result<Domain::AiConnectionTest, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "provider config not found", {}, false});
    Domain::AiConnectionTest test;
    test.uid = m_uids.next();
    test.providerConfigUid = configUid;
    test.testedAt = m_clock.utcIso();
    test.providerModel = config->model;
    const auto capabilityDoc = QJsonDocument::fromJson(
        QByteArray::fromStdString(config->capabilitiesJson));
    test.embeddingRequired = capabilityDoc.isObject()
        && capabilityDoc.object().value(QStringLiteral("embedding")).toBool(false);

    Application::ProviderRequest request;
    request.systemPrompt = "Return exactly one JSON object matching the contract.";
    request.userPrompt = "Return {\"user_text\":\"connection-ok\"}.";
    request.contractType = "generic_v1";
    request.contractVersion = "1";
    const auto response = m_textProvider.submit(*config, request);
    test.authOk = response.ok;
    test.structuredOk = response.ok
        && !Application::ContractValidator::validate("generic_v1", response.structuredJson);
    if (test.embeddingRequired && m_embeddingProvider) {
        const auto embedded = m_embeddingProvider->embed(*config, "connection test");
        test.embeddingOk = embedded.hasValue() && !embedded.value().empty();
    } else {
        test.embeddingOk = !test.embeddingRequired;
    }
    test.overallOk = test.authOk && test.structuredOk && test.embeddingOk;
    QJsonObject capabilities;
    capabilities.insert(QStringLiteral("text"), test.structuredOk);
    capabilities.insert(QStringLiteral("embedding"), test.embeddingOk);
    test.capabilitiesJson = QJsonDocument(capabilities)
                                .toJson(QJsonDocument::Compact).toStdString();
    QJsonObject errors;
    if (!response.ok)
        errors.insert(QStringLiteral("text"), QString::fromStdString(response.errorMessage));
    if (test.embeddingRequired && !test.embeddingOk)
        errors.insert(QStringLiteral("embedding"), QStringLiteral("embedding probe failed"));
    test.errorJson = QJsonDocument(errors).toJson(QJsonDocument::Compact).toStdString();
    const auto saved = m_repository.insertConnectionTest(test);
    if (!saved.ok)
        return Application::Result<Domain::AiConnectionTest, Application::ApplicationError>::failure(
            saved.error);
    return Application::Result<Domain::AiConnectionTest, Application::ApplicationError>::success(test);
}

} // namespace PersonOS::Infrastructure
