#pragma once

#include <optional>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/state/State.h"

// 状态时间线端口（DD-001 §5.3）
namespace PersonOS::Application {

// 最近事件的只读投影（含定义 code；供 R2 检测/界面解析展示）
struct RecentStateEvent
{
    Domain::Uid uid;
    std::string definitionCode;
    std::string valueJson;
    std::string observedAt;
    std::string validUntil;
    std::string idempotencyKey;
};


class StateRepository
{
public:
    virtual ~StateRepository() = default;

    virtual std::optional<Domain::StateDefinition> findDefinitionByCode(
        const std::string &code) = 0;
    virtual SaveResult insertDefinition(const Domain::StateDefinition &definition) = 0;
    virtual std::vector<Domain::StateDefinition> allDefinitions() = 0;

    // 追加式状态事件（幂等键唯一）；不提供 UPDATE/DELETE
    virtual SaveResult appendEvent(const Domain::StateEvent &event) = 0;
    virtual bool existsIdempotencyKey(const std::string &key) = 0;
    // 指定定义在时点的最新有效事件（valid_until >= now，按 observed_at 取最新）
    virtual std::optional<Domain::StateEvent> latestValid(const Domain::Uid &userId,
                                                          const std::string &definitionCode,
                                                          const std::string &nowIso) = 0;
    // 最近事件（R2 检测/界面只读投影；按 observed_at 倒序）
    virtual std::vector<RecentStateEvent> recentEvents(const Domain::Uid &userId,
                                                       int limit) = 0;

    // 用户偏好（user_preference_v3；聊天状态提取开关等授权设置）
    virtual std::optional<std::string> preference(const Domain::Uid &userId,
                                                  const std::string &key) = 0;
    virtual SaveResult setPreference(const Domain::Uid &userId, const std::string &key,
                                     const std::string &valueJson) = 0;
};

} // namespace PersonOS::Application
