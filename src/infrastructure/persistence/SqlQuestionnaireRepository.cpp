#include "infrastructure/persistence/SqlQuestionnaireRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

const char *kQuestionnaireColumns = "uid, code, version_no, name, schema_json, status, revision";

std::optional<Domain::Questionnaire> questionnaireFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::Questionnaire questionnaire;
    questionnaire.uid = *uid;
    questionnaire.code = query.value("code").toString().toStdString();
    questionnaire.versionNo = query.value("version_no").toInt();
    questionnaire.name = query.value("name").toString().toStdString();
    questionnaire.schemaJson = query.value("schema_json").toString().toStdString();
    questionnaire.status = query.value("status").toString().toStdString();
    questionnaire.revision = query.value("revision").toInt();
    return questionnaire;
}

std::optional<Domain::QuestionnaireResponse> responseFromQuery(QSqlQuery &query)
{
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::QuestionnaireResponse response;
    response.uid = *uid;
    response.responsesJson = query.value("responses_json").toString().toStdString();
    response.startedAt = query.value("started_at").toString().toStdString();
    if (!query.value("submitted_at").isNull())
        response.submittedAt = query.value("submitted_at").toString().toStdString();
    response.completionRatio = query.value("completion_ratio").toDouble();
    return response;
}

} // namespace

SqlQuestionnaireRepository::SqlQuestionnaireRepository(QSqlDatabase database,
                                                       const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlQuestionnaireRepository::writeFailure(
    const char *operation, const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

Application::SaveResult SqlQuestionnaireRepository::insertQuestionnaire(
    const Domain::Questionnaire &questionnaire)
{
    if (!questionnaire.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "questionnaire invalid", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO questionnaires_v4(uid, code, version_no, name, schema_json, status, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(questionnaire.uid.value()));
    query.addBindValue(QString::fromStdString(questionnaire.code));
    query.addBindValue(questionnaire.versionNo);
    query.addBindValue(QString::fromStdString(questionnaire.name));
    query.addBindValue(QString::fromStdString(questionnaire.schemaJson));
    query.addBindValue(QString::fromStdString(questionnaire.status));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("questionnaire insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::Questionnaire> SqlQuestionnaireRepository::findByCodeVersion(
    const std::string &code, int versionNo)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questionnaires_v4 WHERE code=? AND version_no=?")
                      .arg(QLatin1String(kQuestionnaireColumns)));
    query.addBindValue(QString::fromStdString(code));
    query.addBindValue(versionNo);
    if (!query.exec() || !query.next())
        return std::nullopt;
    return questionnaireFromQuery(query);
}

std::optional<Domain::Questionnaire> SqlQuestionnaireRepository::findByUid(
    const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM questionnaires_v4 WHERE uid=?").arg(QLatin1String(kQuestionnaireColumns)));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return questionnaireFromQuery(query);
}

std::vector<std::string> SqlQuestionnaireRepository::itemCodesOf(
    const Domain::Uid &questionnaireUid)
{
    std::vector<std::string> out;
    const auto questionnaire = findByUid(questionnaireUid);
    if (!questionnaire)
        return out;

    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(questionnaire->schemaJson));
    if (!document.isObject())
        return out;
    const QJsonArray items = document.object().value(QStringLiteral("items")).toArray();
    for (const auto &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QString code = item.value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            out.push_back(code.toStdString());
    }
    return out;
}

std::optional<Application::ResponseSections> SqlQuestionnaireRepository::parseSections(
    const std::string &responsesJson)
{
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(responsesJson));
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();

    Application::ResponseSections sections;
    const QJsonObject answers = root.value(QStringLiteral("answers")).toObject();
    for (auto it = answers.begin(); it != answers.end(); ++it)
        sections.answers.push_back(it.key().toStdString());
    const QJsonArray noChange = root.value(QStringLiteral("no_change")).toArray();
    for (const auto &value : noChange)
        if (value.isString())
            sections.noChange.push_back(value.toString().toStdString());
    const QJsonArray skipped = root.value(QStringLiteral("skipped")).toArray();
    for (const auto &value : skipped)
        if (value.isString())
            sections.skipped.push_back(value.toString().toStdString());
    return sections;
}

Application::SaveResult SqlQuestionnaireRepository::insertResponse(
    const Domain::QuestionnaireResponse &response)
{
    if (!response.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "questionnaire response invalid", {},
                 false}};

    QSqlQuery reviewQuery(m_database);
    reviewQuery.prepare(QStringLiteral("SELECT id FROM reviews_v4 WHERE uid=?"));
    reviewQuery.addBindValue(QString::fromStdString(response.reviewId.value()));
    if (!reviewQuery.exec() || !reviewQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "review not found", {}, false}};

    QSqlQuery questionnaireQuery(m_database);
    questionnaireQuery.prepare(QStringLiteral("SELECT id FROM questionnaires_v4 WHERE uid=?"));
    questionnaireQuery.addBindValue(QString::fromStdString(response.questionnaireId.value()));
    if (!questionnaireQuery.exec() || !questionnaireQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "questionnaire not found", {}, false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO questionnaire_responses_v4(uid, review_id, questionnaire_id, "
        "responses_json, started_at, submitted_at, completion_ratio) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(response.uid.value()));
    query.addBindValue(reviewQuery.value(0).toLongLong());
    query.addBindValue(questionnaireQuery.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(response.responsesJson));
    query.addBindValue(QString::fromStdString(response.startedAt));
    query.addBindValue(response.submittedAt
                           ? QVariant(QString::fromStdString(*response.submittedAt))
                           : QVariant());
    query.addBindValue(response.completionRatio);
    if (!query.exec())
        return writeFailure("questionnaire response insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlQuestionnaireRepository::updateResponse(
    const Domain::QuestionnaireResponse &response)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE questionnaire_responses_v4 SET responses_json=?, submitted_at=?, "
        "completion_ratio=? WHERE uid=?"));
    query.addBindValue(QString::fromStdString(response.responsesJson));
    query.addBindValue(response.submittedAt
                           ? QVariant(QString::fromStdString(*response.submittedAt))
                           : QVariant());
    query.addBindValue(response.completionRatio);
    query.addBindValue(QString::fromStdString(response.uid.value()));
    if (!query.exec())
        return writeFailure("questionnaire response update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "questionnaire response not found", {},
                 false}};
    return {true, false, {}};
}

std::optional<Domain::QuestionnaireResponse> SqlQuestionnaireRepository::findResponse(
    const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT r.uid, r.responses_json, r.started_at, r.submitted_at, r.completion_ratio "
        "FROM questionnaire_responses_v4 r "
        "WHERE r.review_id=(SELECT id FROM reviews_v4 WHERE uid=?) "
        "AND r.questionnaire_id=(SELECT id FROM questionnaires_v4 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(reviewUid.value()));
    query.addBindValue(QString::fromStdString(questionnaireUid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    auto response = responseFromQuery(query);
    if (response) {
        response->reviewId = reviewUid;
        response->questionnaireId = questionnaireUid;
    }
    return response;
}

} // namespace PersonOS::Infrastructure
