#include "application/audit/Audit.h"

#include <QtGlobal>

namespace PersonOS::Application {

namespace {

AuditSink *g_sink = nullptr;

} // namespace

void Audit::setSink(AuditSink *sink)
{
    g_sink = sink;
}

AuditSink *Audit::sink()
{
    return g_sink;
}

Result<void, ApplicationError> Audit::record(AuditEvent event)
{
    if (!g_sink)
        return Result<void, ApplicationError>::success();
    const auto result = g_sink->append(std::move(event));
    // 用例层忽略返回值：注册 sink 后写入失败必须留痕，不能静默丢审计
    if (!result.hasValue())
        qWarning("audit append failed: %s", result.error().message.c_str());
    return result;
}

} // namespace PersonOS::Application
