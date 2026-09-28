#pragma once

#include <QSqlDatabase>

#include "application/ports/CalibrationRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlCalibrationRepository final : public Application::CalibrationRepository
{
public:
    explicit SqlCalibrationRepository(QSqlDatabase database, const Domain::Clock &clock);

    Application::SaveResult insert(const Domain::CalibrationRecord &record) override;
    std::optional<Domain::CalibrationRecord> activeValue(
        const Domain::Uid &userId, const std::string &parameterCode) override;
    Domain::MelOutcomeFacts melOutcomeFacts(const Domain::Uid &melUid) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
