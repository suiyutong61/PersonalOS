#pragma once

#include "application/ports/AiPorts.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "infrastructure/ai/SqlAiRepository.h"

namespace PersonOS::Infrastructure {

class ModelConnectionTestService
{
public:
    ModelConnectionTestService(SqlAiRepository &repository,
                               Application::AiProviderPort &textProvider,
                               Application::AiEmbeddingProviderPort *embeddingProvider,
                               Application::UuidPort &uids, const Domain::Clock &clock);
    Application::Result<Domain::AiConnectionTest, Application::ApplicationError> test(
        const Domain::Uid &configUid);

private:
    SqlAiRepository &m_repository;
    Application::AiProviderPort &m_textProvider;
    Application::AiEmbeddingProviderPort *m_embeddingProvider;
    Application::UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
