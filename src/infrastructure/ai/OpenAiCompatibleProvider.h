#pragma once

#include <QObject>

#include "application/foundation/Result.h"
#include "application/ports/AiPorts.h"

#include <vector>

// OpenAI 兼容协议 Provider 适配器（DR-015/025）
// 实现 AiProviderPort：POST {endpoint}/chat/completions，凭据经系统凭据库读取；
// 不持有/不记录 API Key；返回面向用户说明与结构化 JSON 双通道（尽力解析）。
namespace PersonOS::Infrastructure {

class OpenAiCompatibleProvider : public QObject, public Application::AiProviderPort,
                                 public Application::AiEmbeddingProviderPort
{
    Q_OBJECT
public:
    explicit OpenAiCompatibleProvider(Application::CredentialStorePort &credentials,
                                      QObject *parent = nullptr);

    Application::ProviderResponse submit(const Domain::AiProviderConfig &config,
                                         const Application::ProviderRequest &request) override;

    // 嵌入（POST {endpoint}/embeddings → data[0].embedding）；凭据同 submit
    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const Domain::AiProviderConfig &config, const std::string &text);

private:
    Application::CredentialStorePort &m_credentials;
};

} // namespace PersonOS::Infrastructure
