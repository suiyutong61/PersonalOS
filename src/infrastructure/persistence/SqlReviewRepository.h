#pragma once

#include <QSqlDatabase>

#include "application/ports/ReviewRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlReviewRepository final : public Application::ReviewRepository
{
public:
    explicit SqlReviewRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::Review> findByMel(const Domain::Uid &melId) override;
    std::optional<Domain::Review> findByUid(const Domain::Uid &uid) override;
    std::vector<Domain::Review> listRecent(int limit) override;
    Application::SaveResult insert(const Domain::Review &review) override;
    Application::SaveResult update(const Domain::Review &review, int expectedRevision) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
