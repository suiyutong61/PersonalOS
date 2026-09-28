#pragma once

#include <QSqlDatabase>

#include "application/ports/DomainManifestRepository.h"

namespace PersonOS::Infrastructure {

class SqlDomainManifestRepository final : public Application::DomainManifestRepository
{
public:
    explicit SqlDomainManifestRepository(QSqlDatabase database);

    std::optional<Domain::Uid> findManifestByCode(const std::string &domainCode) override;
    std::optional<Application::ManifestVersionRecord> latestActiveVersion(
        const Domain::Uid &manifestUid) override;
    std::optional<Application::ManifestVersionRecord> versionByUid(
        const std::string &versionUid) override;

private:
    QSqlDatabase m_database;
};

} // namespace PersonOS::Infrastructure
