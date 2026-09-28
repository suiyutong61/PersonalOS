#pragma once

#include <string>

#include "application/audit/AuditEvent.h"
#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

// 审计写入入口（DR-026）：业务用例在正式状态变更成功后立即追加审计事件。
// 通过注册的 AuditSink 落库（组合根/测试注册 SqlAuditRepository 或内存替身）；
// 未注册时静默跳过（保持既有用例可脱离基础设施运行），注册后写入失败返回错误。
// 注意：当前用例层按仓库自动提交执行，审计写入紧随业务写入同一调用完成；
// 完整事务耦合随 UnitOfWork 迁移推进（见追踪文件）。
namespace PersonOS::Application {

class AuditSink
{
public:
    virtual ~AuditSink() = default;
    virtual Result<void, ApplicationError> append(const AuditEvent &event) = 0;
};

namespace Audit {

void setSink(AuditSink *sink);
AuditSink *sink();

// 记录审计事件；无 sink 时返回 success（no-op）。
Result<void, ApplicationError> record(AuditEvent event);

} // namespace Audit

} // namespace PersonOS::Application
