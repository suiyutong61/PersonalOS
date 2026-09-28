#pragma once

#include <QSqlDatabase>

#include "application/ports/RetentionRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlRetentionRepository final : public Application::RetentionRepository
{
public:
    explicit SqlRetentionRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::RetentionSchedule> findByUid(const Domain::Uid &uid) override;
    Application::SaveResult insert(const Domain::RetentionSchedule &schedule) override;
    Application::SaveResult update(const Domain::RetentionSchedule &schedule,
                                   int expectedRevision) override;
    std::vector<Application::RetentionCandidate> dueCandidates(
        const Domain::Uid &userId, const std::string &nowIso, int limit) override;
    std::vector<Domain::RetentionSchedule> listForUser(const Domain::Uid &userId) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
