#include "infrastructure/knowledge/SqlContentIngestRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlContentIngestRepository::SqlContentIngestRepository(QSqlDatabase database,
                                                       const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

std::optional<Domain::ContentImportJob> SqlContentIngestRepository::findByUid(
    const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, source_type, source_uri, status, stage, error_code, error_detail, "
        "idempotency_key, requested_by, started_at, completed_at, revision "
        "FROM content_import_jobs_v6 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::ContentImportJob job;
    job.uid = *parsed;
    job.sourceType = query.value(1).toString().toStdString();
    job.sourceUri = query.value(2).toString().toStdString();
    if (const auto status = Domain::ingestStatusFrom(query.value(3).toString().toStdString()))
        job.status = *status;
    job.stage = query.value(4).toString().toStdString();
    if (!query.value(5).isNull())
        job.errorCode = query.value(5).toString().toStdString();
    if (!query.value(6).isNull())
        job.errorDetail = query.value(6).toString().toStdString();
    job.idempotencyKey = query.value(7).toString().toStdString();
    job.requestedBy = query.value(8).toString().toStdString();
    if (!query.value(9).isNull())
        job.startedAt = query.value(9).toString().toStdString();
    if (!query.value(10).isNull())
        job.completedAt = query.value(10).toString().toStdString();
    job.revision = query.value(11).toInt();
    return job;
}

Application::SaveResult SqlContentIngestRepository::insert(
    const Domain::ContentImportJob &job)
{
    if (!job.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "content import job invalid", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO content_import_jobs_v6(uid, source_type, source_uri, status, stage, "
        "error_code, error_detail, idempotency_key, requested_by, started_at, completed_at, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(job.uid.value()));
    query.addBindValue(QString::fromStdString(job.sourceType));
    query.addBindValue(QString::fromStdString(job.sourceUri));
    query.addBindValue(QString::fromStdString(Domain::toString(job.status)));
    query.addBindValue(QString::fromStdString(job.stage));
    query.addBindValue(job.errorCode
                           ? QVariant(QString::fromStdString(*job.errorCode))
                           : QVariant());
    query.addBindValue(job.errorDetail
                           ? QVariant(QString::fromStdString(*job.errorDetail))
                           : QVariant());
    query.addBindValue(QString::fromStdString(job.idempotencyKey));
    query.addBindValue(QString::fromStdString(job.requestedBy));
    query.addBindValue(job.startedAt
                           ? QVariant(QString::fromStdString(*job.startedAt))
                           : QVariant());
    query.addBindValue(job.completedAt
                           ? QVariant(QString::fromStdString(*job.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    if (!query.exec())
        return {false, false,
                {Application::ErrorCode::Storage, "content import job insert failed",
                 query.lastError().text().toStdString(), false}};
    return {true, false, {}};
}

Application::SaveResult SqlContentIngestRepository::update(
    const Domain::ContentImportJob &job, int expectedRevision)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE content_import_jobs_v6 SET status=?, stage=?, error_code=?, error_detail=?, "
        "started_at=?, completed_at=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(job.status)));
    query.addBindValue(QString::fromStdString(job.stage));
    query.addBindValue(job.errorCode
                           ? QVariant(QString::fromStdString(*job.errorCode))
                           : QVariant());
    query.addBindValue(job.errorDetail
                           ? QVariant(QString::fromStdString(*job.errorDetail))
                           : QVariant());
    query.addBindValue(job.startedAt
                           ? QVariant(QString::fromStdString(*job.startedAt))
                           : QVariant());
    query.addBindValue(job.completedAt
                           ? QVariant(QString::fromStdString(*job.completedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    query.addBindValue(QString::fromStdString(job.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return {false, false,
                {Application::ErrorCode::Storage, "content import job update failed",
                 query.lastError().text().toStdString(), false}};
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "content import job revision conflict", {},
                 false}};
    return {true, false, {}};
}

bool SqlContentIngestRepository::existsIdempotencyKey(const std::string &key)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM content_import_jobs_v6 WHERE idempotency_key=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(key));
    return query.exec() && query.next();
}

} // namespace PersonOS::Infrastructure
