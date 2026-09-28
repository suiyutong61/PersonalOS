#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/audit/AuditEvent.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Uid.h"

// 审计仓储端口（DD-001 §5.3；数据库设计 §6 audit_events_v6）
// 追加式记录，不提供 UPDATE/DELETE；撤销/删除产生新事件保留过程。
namespace PersonOS::Application {

class AuditRepository
{
public:
    virtual ~AuditRepository() = default;

    virtual Result<void, ApplicationError> append(const AuditEvent &event,
                                                  const std::string &occurredAtIso) = 0;

    // 按对象查询审计历史（追溯用）
    virtual std::vector<AuditEvent> eventsOf(const std::string &aggregateType,
                                             const std::string &aggregateUid,
                                             int limit) = 0;
};

} // namespace PersonOS::Application
