#pragma once

#include <QSqlDatabase>

#include "application/audit/Audit.h"
#include "application/ports/AuditRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlAuditRepository final : public Application::AuditRepository,
                                 public Application::AuditSink
{
public:
    explicit SqlAuditRepository(QSqlDatabase database, const Domain::Clock &clock,
                                Application::UuidPort &uids);

    // AuditRepository
    Application::Result<void, Application::ApplicationError> append(
        const Application::AuditEvent &event, const std::string &occurredAtIso) override;
    std::vector<Application::AuditEvent> eventsOf(const std::string &aggregateType,
                                                  const std::string &aggregateUid,
                                                  int limit) override;

    // AuditSink（用例层经 Audit::record 调用；时间由本仓库时钟生成）
    Application::Result<void, Application::ApplicationError> append(
        const Application::AuditEvent &event) override;

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
    Application::UuidPort &m_uids;
};

} // namespace PersonOS::Infrastructure
