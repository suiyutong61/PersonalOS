#pragma once

#include <optional>
#include <string>

// 审计事件（requirements 10.6.1；DR-026；数据库设计 §6 audit_events_v6）
// 重要业务变更追加式记录，不被普通修改覆盖；不保存 API Key/密码等秘密。
namespace PersonOS::Application {

struct AuditEvent
{
    std::string actorType;            // user / system / ai
    std::string actorRef;             // 操作者引用（如用户 uid；可空）
    std::string action;               // 如 mel.settled / goal.created / knowledge.imported
    std::string aggregateType;        // 对象类型：mel / goal / route / review / knowledge / ...
    std::string aggregateUid;         // 对象 uid
    std::string detailJson = "{}";    // 变更摘要（脱敏，无凭据）
    std::optional<std::string> correlationUid;   // 关联编号（同一业务动作的多次变更）
};

} // namespace PersonOS::Application
