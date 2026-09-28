#pragma once

#include <optional>

#include "domain/ai/Ai.h"
#include "domain/foundation/Uid.h"

// AI 连接配置只读接口（设置页/规划管线共用；SqlAiRepository 实现）
namespace PersonOS::Application {

class AiConfigStore
{
public:
    virtual ~AiConfigStore() = default;
    virtual std::optional<Domain::AiProviderConfig> findFirstEnabledConfig() = 0;
    virtual std::optional<Domain::AiProviderConfig> findConfig(const Domain::Uid &uid) = 0;
};

} // namespace PersonOS::Application
