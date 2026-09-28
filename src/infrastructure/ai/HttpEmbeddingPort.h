#pragma once

#include <vector>

#include "application/ports/AiConfigStore.h"
#include "application/ports/AiPorts.h"      // CredentialStorePort
#include "application/ports/EmbeddingPort.h"

// 经 AI 网关的嵌入实现：选择启用的、声明 embedding 能力的模型连接，
// 调用 OpenAI 兼容 /embeddings 端点；不可用/未声明能力时明确失败，
// 检索侧据此降级为结构化+全文（DR-013/025）。
namespace PersonOS::Infrastructure {

class HttpEmbeddingPort final : public Application::EmbeddingPort
{
public:
    HttpEmbeddingPort(Application::AiConfigStore &configs,
                      Application::CredentialStorePort &credentials);

    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const std::string &text) override;
    int dimension() const override { return m_dimension; }

private:
    Application::AiConfigStore &m_configs;
    Application::CredentialStorePort &m_credentials;
    int m_dimension = 0;   // 首次成功调用后缓存
};

} // namespace PersonOS::Infrastructure
