#pragma once

#include <QSqlDatabase>

#include "application/ports/RouteRepository.h"
#include "domain/foundation/Clock.h"

// SQLite 路线仓储（数据库设计 §3.3 routes_v3/route_versions_v3/route_stages_v3）
namespace PersonOS::Infrastructure {

class SqlRouteRepository final : public Application::RouteRepository
{
public:
    explicit SqlRouteRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::Route> findByUid(const Domain::Uid &uid) override;
    std::vector<Domain::Route> findByGoal(const Domain::Uid &goalId) override;
    Application::SaveResult insert(const Domain::Route &route) override;
    Application::SaveResult update(const Domain::Route &route, int expectedRevision) override;
    Application::Result<std::string, Application::ApplicationError> insertVersion(
        const Domain::Uid &routeId, const Domain::RouteVersion &version) override;
    std::vector<Domain::RouteVersion> versionsOf(const Domain::Uid &routeId) override;
    Application::SaveResult markVersionConfirmed(const Domain::Uid &routeId, int versionNo,
                                                 const std::string &confirmedAtIso) override;

private:
    Application::SaveResult writeFailure(const char *operation, const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
