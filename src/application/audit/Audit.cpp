#include "application/audit/Audit.h"

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
    return g_sink->append(std::move(event));
}

} // namespace PersonOS::Application
