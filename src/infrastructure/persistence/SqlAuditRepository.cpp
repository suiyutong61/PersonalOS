#include "infrastructure/persistence/SqlAuditRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

Application::ApplicationError storageError(const char *operation, const QSqlQuery &query)
{
    return {Application::ErrorCode::Storage, operation,
            query.lastError().text().toStdString(), false};
}

} // namespace

SqlAuditRepository::SqlAuditRepository(QSqlDatabase database, const Domain::Clock &clock,
                                       Application::UuidPort &uids)
    : m_database(std::move(database)), m_clock(clock), m_uids(uids)
{}

Application::Result<void, Application::ApplicationError> SqlAuditRepository::append(
    const Application::AuditEvent &event, const std::string &occurredAtIso)
{
    if (event.action.empty() || event.aggregateType.empty() || event.aggregateUid.empty())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "audit event invalid", {}, false});

    const std::string uid = m_uids.next().value();
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO audit_events_v6(uid, actor_type, actor_ref, action, aggregate_type, "
        "aggregate_uid, before_hash, after_hash, detail_json, occurred_at, correlation_uid) "
        "VALUES(?,?,?,?,?,?,NULL,NULL,?,?,?)"));
    query.addBindValue(QString::fromStdString(uid));
    query.addBindValue(QString::fromStdString(event.actorType));
    query.addBindValue(event.actorRef.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(event.actorRef)));
    query.addBindValue(QString::fromStdString(event.action));
    query.addBindValue(QString::fromStdString(event.aggregateType));
    query.addBindValue(QString::fromStdString(event.aggregateUid));
    query.addBindValue(QString::fromStdString(
        event.detailJson.empty() ? std::string("{}") : event.detailJson));
    query.addBindValue(QString::fromStdString(occurredAtIso));
    query.addBindValue(event.correlationUid
                           ? QVariant(QString::fromStdString(*event.correlationUid))
                           : QVariant());
    if (!query.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            storageError("audit insert failed", query));
    return Application::Result<void, Application::ApplicationError>::success();
}

Application::Result<void, Application::ApplicationError> SqlAuditRepository::append(
    const Application::AuditEvent &event)
{
    return append(event, formatUtcIso(m_clock.now()));
}

std::vector<Application::AuditEvent> SqlAuditRepository::eventsOf(
    const std::string &aggregateType, const std::string &aggregateUid, int limit)
{
    std::vector<Application::AuditEvent> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT actor_type, actor_ref, action, detail_json, occurred_at, correlation_uid "
        "FROM audit_events_v6 WHERE aggregate_type=? AND aggregate_uid=? "
        "ORDER BY occurred_at DESC, id DESC LIMIT ?"));
    query.addBindValue(QString::fromStdString(aggregateType));
    query.addBindValue(QString::fromStdString(aggregateUid));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        Application::AuditEvent event;
        event.actorType = query.value(0).toString().toStdString();
        if (!query.value(1).isNull())
            event.actorRef = query.value(1).toString().toStdString();
        event.action = query.value(2).toString().toStdString();
        event.detailJson = query.value(3).toString().toStdString();
        event.aggregateType = aggregateType;
        event.aggregateUid = aggregateUid;
        if (!query.value(5).isNull())
            event.correlationUid = query.value(5).toString().toStdString();
        out.push_back(std::move(event));
    }
    return out;
}

} // namespace PersonOS::Infrastructure
