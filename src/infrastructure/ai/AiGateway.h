#pragma once

#include "application/foundation/ContractValidator.h"
#include "application/ports/AiPorts.h"
#include "application/ports/UuidPort.h"
#include "infrastructure/ai/SqlAiRepository.h"

// AI 网关实现（DD-001 §9；DR-015/016/025/027）
// 流程：登记任务（幂等）→ 执行（provider 提交 → 结构验证 → 重试/终态）→
// 调用记录与上下文项落库；知识支持程度随请求固化。
namespace PersonOS::Infrastructure {

class AiGateway final : public Application::AiGatewayPort
{
public:
    AiGateway(SqlAiRepository &repo, Application::AiProviderPort &provider, Application::UuidPort &uids,
              const Domain::Clock &clock);

    Application::Result<Domain::AiJob, Application::ApplicationError> submit(
        const Application::AiGatewaySubmit &input) override;
    Application::Result<Domain::AiJob, Application::ApplicationError> execute(
        const Domain::Uid &jobUid) override;

private:
    SqlAiRepository &m_repo;
    Application::AiProviderPort &m_provider;
    Application::UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
