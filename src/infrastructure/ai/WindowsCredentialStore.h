#pragma once

#include "application/ports/AiPorts.h"

// Windows 系统凭据库适配器（DR-025：Credential Manager，CRED_TYPE_GENERIC）
// 密钥绝不进入 SQLite / 日志 / 源码；SQLite 只保存 credential_ref。
namespace PersonOS::Infrastructure {

class WindowsCredentialStore final : public Application::CredentialStorePort
{
public:
    Application::Result<void, Application::ApplicationError> write(
        const std::string &reference, const std::string &secret) override;
    Application::Result<std::string, Application::ApplicationError> read(
        const std::string &reference) override;
    Application::Result<void, Application::ApplicationError> remove(
        const std::string &reference) override;
};

} // namespace PersonOS::Infrastructure
