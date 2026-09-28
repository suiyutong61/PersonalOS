#include "infrastructure/persistence/SqlDomainManifestRepository.h"

#include <QSqlQuery>

namespace PersonOS::Infrastructure {

namespace {

std::optional<Application::ManifestVersionRecord> recordFromQuery(QSqlQuery &query)
{
    Application::ManifestVersionRecord record;
    record.versionUid = query.value("uid").toString().toStdString();
    record.versionNo = query.value("version_no").toInt();
    record.manifestJson = query.value("manifest_json").toString().toStdString();
    record.schemaVersion = query.value("schema_version").toString().toStdString();
    record.contentHash = query.value("content_hash").toString().toStdString();
    record.validFrom = query.value("valid_from").toString().toStdString();
    if (record.versionUid.empty() || record.manifestJson.empty())
        return std::nullopt;
    return record;
}

} // namespace

SqlDomainManifestRepository::SqlDomainManifestRepository(QSqlDatabase database)
    : m_database(std::move(database))
{}

std::optional<Domain::Uid> SqlDomainManifestRepository::findManifestByCode(
    const std::string &domainCode)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM domain_manifests_v3 WHERE domain_code=? ORDER BY id"));
    query.addBindValue(QString::fromStdString(domainCode));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return Domain::Uid::parse(query.value(0).toString().toStdString());
}

std::optional<Application::ManifestVersionRecord>
SqlDomainManifestRepository::latestActiveVersion(const Domain::Uid &manifestUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, version_no, manifest_json, schema_version, content_hash, valid_from "
        "FROM domain_manifest_versions_v3 "
        "WHERE manifest_id=(SELECT id FROM domain_manifests_v3 WHERE uid=?) "
        "AND status='active' ORDER BY version_no DESC LIMIT 1"));
    query.addBindValue(QString::fromStdString(manifestUid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return recordFromQuery(query);
}

std::optional<Application::ManifestVersionRecord> SqlDomainManifestRepository::versionByUid(
    const std::string &versionUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, version_no, manifest_json, schema_version, content_hash, valid_from "
        "FROM domain_manifest_versions_v3 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(versionUid));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return recordFromQuery(query);
}

} // namespace PersonOS::Infrastructure
