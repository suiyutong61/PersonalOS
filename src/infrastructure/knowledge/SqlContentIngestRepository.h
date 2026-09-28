#pragma once

#include <QSqlDatabase>

#include "application/ports/ContentPorts.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlContentIngestRepository final : public Application::ContentIngestRepositoryPort
{
public:
    explicit SqlContentIngestRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::ContentImportJob> findByUid(const Domain::Uid &uid) override;
    Application::SaveResult insert(const Domain::ContentImportJob &job) override;
    Application::SaveResult update(const Domain::ContentImportJob &job,
                                   int expectedRevision) override;
    bool existsIdempotencyKey(const std::string &key) override;

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
