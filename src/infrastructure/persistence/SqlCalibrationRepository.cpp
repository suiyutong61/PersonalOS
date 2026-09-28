#include "infrastructure/persistence/SqlCalibrationRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

std::optional<Domain::CalibrationRecord> recordFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::CalibrationRecord record;
    record.uid = *uid;
    record.parameterCode = query.value("parameter_code").toString().toStdString();
    if (!query.value("old_value_json").isNull())
        record.oldValueJson = query.value("old_value_json").toString().toStdString();
    record.newValueJson = query.value("new_value_json").toString().toStdString();
    record.evidenceJson = query.value("evidence_json").toString().toStdString();
    record.effectiveFrom = query.value("effective_from").toString().toStdString();
    record.createdAt = query.value("created_at").toString().toStdString();
    if (!query.value("decision_uid").isNull())
        record.decisionUid = query.value("decision_uid").toString().toStdString();
    return record;
}

} // namespace

SqlCalibrationRepository::SqlCalibrationRepository(QSqlDatabase database,
                                                   const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlCalibrationRepository::writeFailure(const char *operation,
                                                               const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

Application::SaveResult SqlCalibrationRepository::insert(
    const Domain::CalibrationRecord &record)
{
    if (!record.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "calibration record invalid", {}, false}};

    QSqlQuery userQuery(m_database);
    userQuery.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    userQuery.addBindValue(QString::fromStdString(record.userId.value()));
    if (!userQuery.exec() || !userQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};
    const qint64 userId = userQuery.value(0).toLongLong();

    QVariant manifestId;
    if (record.domainManifestId) {
        QSqlQuery manifestQuery(m_database);
        manifestQuery.prepare(QStringLiteral("SELECT id FROM domain_manifests_v3 WHERE uid=?"));
        manifestQuery.addBindValue(QString::fromStdString(record.domainManifestId->value()));
        if (!manifestQuery.exec() || !manifestQuery.next())
            return {false, false,
                    {Application::ErrorCode::NotFound, "domain manifest not found", {}, false}};
        manifestId = manifestQuery.value(0).toLongLong();
    }

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO calibration_records_v4(uid, user_id, domain_manifest_id, parameter_code, "
        "old_value_json, new_value_json, evidence_json, effective_from, created_at, "
        "decision_uid) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(record.uid.value()));
    query.addBindValue(userId);
    query.addBindValue(manifestId);
    query.addBindValue(QString::fromStdString(record.parameterCode));
    query.addBindValue(record.oldValueJson
                           ? QVariant(QString::fromStdString(*record.oldValueJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(record.newValueJson));
    query.addBindValue(QString::fromStdString(record.evidenceJson));
    query.addBindValue(QString::fromStdString(record.effectiveFrom));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(record.decisionUid
                           ? QVariant(QString::fromStdString(*record.decisionUid))
                           : QVariant());
    if (!query.exec())
        return writeFailure("calibration record insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::CalibrationRecord> SqlCalibrationRepository::activeValue(
    const Domain::Uid &userId, const std::string &parameterCode)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, parameter_code, old_value_json, new_value_json, evidence_json, "
        "effective_from, created_at, decision_uid FROM calibration_records_v4 "
        "WHERE user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) AND parameter_code=? "
        "ORDER BY effective_from DESC, id DESC LIMIT 1"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(parameterCode));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto record = recordFromQuery(query);
    if (record)
        record->userId = userId;
    return record;
}

Domain::MelOutcomeFacts SqlCalibrationRepository::melOutcomeFacts(const Domain::Uid &melUid)
{
    Domain::MelOutcomeFacts facts;

    QSqlQuery melQuery(m_database);
    melQuery.prepare(QStringLiteral(
        "SELECT m.id, u.uid FROM mels_v4 m JOIN user_profiles_v3 u ON u.id=m.user_id "
        "WHERE m.uid=?"));
    melQuery.addBindValue(QString::fromStdString(melUid.value()));
    if (!melQuery.exec() || !melQuery.next())
        return facts;   // melExists = false
    facts.melExists = true;
    if (const auto userUid = Domain::Uid::parse(
            melQuery.value("uid").toString().toStdString()))
        facts.userId = *userUid;
    const qint64 melPk = melQuery.value(0).toLongLong();

    QSqlQuery prediction(m_database);
    prediction.prepare(QStringLiteral(
        "SELECT predicted_completion, predicted_effort_min FROM mel_predictions_v4 "
        "WHERE mel_id=? AND superseded_at IS NULL ORDER BY created_at DESC, id DESC LIMIT 1"));
    prediction.addBindValue(melPk);
    if (prediction.exec() && prediction.next()) {
        facts.hasPrediction = true;
        facts.predictedCompletion = prediction.value("predicted_completion").toDouble();
        facts.predictedEffortMin = prediction.value("predicted_effort_min").toInt();
    }

    QSqlQuery tasks(m_database);
    tasks.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(progress),0.0), "
        "COALESCE(SUM(progress*planned_effort_min),0.0), "
        "COALESCE(SUM(planned_effort_min),0) FROM mel_tasks_v4 WHERE mel_id=? AND required=1"));
    tasks.addBindValue(melPk);
    if (tasks.exec() && tasks.next()) {
        facts.requiredTaskCount = tasks.value(0).toInt();
        facts.taskProgressSum = tasks.value(1).toDouble();
        facts.weightedProgressSum = tasks.value(2).toDouble();
        facts.plannedEffortSum = tasks.value(3).toInt();
    }

    QSqlQuery events(m_database);
    events.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount),0) FROM progress_events_v4 "
        "WHERE mel_id=? AND unit='minutes'"));
    events.addBindValue(melPk);
    if (events.exec() && events.next())
        facts.actualMinutesSum = events.value(0).toInt();

    return facts;
}

} // namespace PersonOS::Infrastructure
