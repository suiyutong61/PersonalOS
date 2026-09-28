#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/ai/Ai.h"
#include "domain/foundation/Uid.h"

// AI 相关端口（DD-001 §5.4）：凭据库、模型 Provider、AI 网关
namespace PersonOS::Application {

// 系统凭据库（DR-025：密钥只存系统凭据库；SQLite 只保存 credential_ref）
class CredentialStorePort
{
public:
    virtual ~CredentialStorePort() = default;
    virtual Result<void, ApplicationError> write(const std::string &reference,
                                                 const std::string &secret) = 0;
    virtual Result<std::string, ApplicationError> read(const std::string &reference) = 0;
    virtual Result<void, ApplicationError> remove(const std::string &reference) = 0;
};

// 模型 Provider（OpenAI 兼容等适配器实现；DR-015 双通道：自然语言 + 结构化 JSON）
struct ProviderRequest
{
    std::string systemPrompt;
    std::string userPrompt;
    std::string contractType;         // 响应契约类型（如 mel_proposal_v1）
    std::string contractVersion;
};

struct ProviderResponse
{
    bool ok = false;
    std::string userText;             // 面向用户的说明通道
    std::string structuredJson;       // 结构化动作通道（可为空）
    std::string errorMessage;
};

class AiProviderPort
{
public:
    virtual ~AiProviderPort() = default;
    virtual ProviderResponse submit(const Domain::AiProviderConfig &config,
                                    const ProviderRequest &request) = 0;
};

class AiEmbeddingProviderPort
{
public:
    virtual ~AiEmbeddingProviderPort() = default;
    virtual Result<std::vector<float>, ApplicationError> embed(
        const Domain::AiProviderConfig &config, const std::string &text) = 0;
};

// AI 网关：任务登记 → 执行（校验/重试/降级标记）→ 结果落库
struct AiContextItem
{
    std::string contextType;          // knowledge/state/protocol/...
    std::string contextUid;
    std::string versionUid;
    int rank = 0;
    std::string reason;
};

struct AiGatewaySubmit
{
    Domain::Uid providerConfigUid;
    std::string jobType;
    std::string contractType;
    std::string contractVersion;
    std::string requestJson;
    std::string idempotencyKey;       // 必填，唯一（DB-06）
    Domain::SourceMode sourceMode;    // 知识支持程度（DR-027）
    std::vector<AiContextItem> contextItems;
    int maxAttempts = 3;
};

class AiGatewayPort
{
public:
    virtual ~AiGatewayPort() = default;

    // 登记任务（持久化；重复幂等键返回 Conflict）
    virtual Result<Domain::AiJob, ApplicationError> submit(const AiGatewaySubmit &input) = 0;

    // 执行一次任务（provider → 结构验证 → 重试/终态）；测试可注入 Fake Provider
    virtual Result<Domain::AiJob, ApplicationError> execute(const Domain::Uid &jobUid) = 0;
};

} // namespace PersonOS::Application
