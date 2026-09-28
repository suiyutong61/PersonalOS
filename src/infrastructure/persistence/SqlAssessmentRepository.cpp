#include "infrastructure/persistence/SqlAssessmentRepository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlAssessmentRepository::SqlAssessmentRepository(QSqlDatabase database,
                                                 const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlAssessmentRepository::writeFailure(const char *operation,
                                                               const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<qint64> SqlAssessmentRepository::resolvePk(const char *sql,
                                                         const Domain::Uid &uid) const
{
    QSqlQuery query(m_database);
    query.prepare(QString::fromLatin1(sql));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

std::optional<Domain::Assessment> SqlAssessmentRepository::findByUid(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT a.uid, u.uid AS user_uid, g.uid AS goal_uid, m.uid AS mel_uid, "
        "a.assessment_type, a.status, a.scope_json, a.rubric_json, a.generated_by, "
        "a.knowledge_snapshot_uid, a.revision "
        "FROM assessments_v4 a "
        "LEFT JOIN user_profiles_v3 u ON u.id=a.user_id "
        "LEFT JOIN goals_v3 g ON g.id=a.goal_id "
        "LEFT JOIN mels_v4 m ON m.id=a.mel_id WHERE a.uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto userId = Domain::Uid::parse(query.value("user_uid").toString().toStdString());
    const auto goalId = Domain::Uid::parse(query.value("goal_uid").toString().toStdString());
    if (!parsed || !userId || !goalId)
        return std::nullopt;
    Domain::Assessment assessment;
    assessment.uid = *parsed;
    assessment.userId = *userId;
    assessment.goalId = *goalId;
    if (!query.value("mel_uid").isNull())
        if (const auto mel = Domain::Uid::parse(query.value("mel_uid").toString().toStdString()))
            assessment.melId = *mel;
    assessment.assessmentType = query.value("assessment_type").toString().toStdString();
    if (const auto status =
            Domain::assessmentStatusFrom(query.value("status").toString().toStdString()))
        assessment.status = *status;
    assessment.scopeJson = query.value("scope_json").toString().toStdString();
    assessment.rubricJson = query.value("rubric_json").toString().toStdString();
    assessment.generatedBy = query.value("generated_by").toString().toStdString();
    if (!query.value("knowledge_snapshot_uid").isNull())
        assessment.knowledgeSnapshotUid =
            query.value("knowledge_snapshot_uid").toString().toStdString();
    assessment.revision = query.value("revision").toInt();
    return assessment;
}

std::vector<Domain::Assessment> SqlAssessmentRepository::listForUser(const Domain::Uid &userId,
                                                                     int limit)
{
    std::vector<Domain::Assessment> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT a.uid FROM assessments_v4 a WHERE a.user_id="
        "(SELECT id FROM user_profiles_v3 WHERE uid=?) ORDER BY a.id DESC LIMIT ?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto assessment = findByUid(*uid))
            out.push_back(*assessment);
    }
    return out;
}

Application::SaveResult SqlAssessmentRepository::insert(const Domain::Assessment &assessment)
{
    if (!assessment.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "assessment invalid", {}, false}};
    const auto userPk = resolvePk("SELECT id FROM user_profiles_v3 WHERE uid=?",
                                  assessment.userId);
    const auto goalPk = resolvePk("SELECT id FROM goals_v3 WHERE uid=?", assessment.goalId);
    if (!userPk || !goalPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "user/goal not found", {}, false}};
    std::optional<qint64> melPk;
    if (assessment.melId) {
        melPk = resolvePk("SELECT id FROM mels_v4 WHERE uid=?", *assessment.melId);
        if (!melPk)
            return {false, false,
                    {Application::ErrorCode::NotFound, "mel not found", {}, false}};
    }

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO assessments_v4(uid, user_id, mel_id, goal_id, assessment_type, status, "
        "scope_json, rubric_json, generated_by, knowledge_snapshot_uid, created_at, "
        "updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(assessment.uid.value()));
    query.addBindValue(*userPk);
    query.addBindValue(melPk ? QVariant(*melPk) : QVariant());
    query.addBindValue(*goalPk);
    query.addBindValue(QString::fromStdString(assessment.assessmentType));
    query.addBindValue(QString::fromStdString(Domain::toString(assessment.status)));
    query.addBindValue(QString::fromStdString(
        assessment.scopeJson.empty() ? std::string("{}") : assessment.scopeJson));
    query.addBindValue(QString::fromStdString(
        assessment.rubricJson.empty() ? std::string("{}") : assessment.rubricJson));
    query.addBindValue(QString::fromStdString(assessment.generatedBy));
    query.addBindValue(assessment.knowledgeSnapshotUid
                           ? QVariant(QString::fromStdString(*assessment.knowledgeSnapshotUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("assessment insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAssessmentRepository::update(const Domain::Assessment &assessment,
                                                        int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE assessments_v4 SET status=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(assessment.status)));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(assessment.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("assessment update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "assessment revision conflict", {}, false}};
    return {true, false, {}};
}

Application::SaveResult SqlAssessmentRepository::insertItem(const Domain::AssessmentItem &item)
{
    const auto assessmentPk =
        resolvePk("SELECT id FROM assessments_v4 WHERE uid=?", item.assessmentId);
    if (!assessmentPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "assessment not found", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    const std::string rubric = item.rubricJson.empty() ? std::string("{}") : item.rubricJson;
    const std::string sources =
        item.sourceRefsJson.empty() ? std::string("[]") : item.sourceRefsJson;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO assessment_items_v4(uid, assessment_id, item_type, prompt, "
        "reference_answer, rubric_json, source_refs_json, sequence_no, difficulty, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(item.uid.value()));
    query.addBindValue(*assessmentPk);
    query.addBindValue(QString::fromStdString(item.itemType));
    query.addBindValue(QString::fromStdString(item.prompt));
    query.addBindValue(item.referenceAnswer.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(item.referenceAnswer)));
    query.addBindValue(QString::fromStdString(rubric));
    query.addBindValue(QString::fromStdString(sources));
    query.addBindValue(item.sequenceNo);
    query.addBindValue(item.difficulty ? QVariant(*item.difficulty) : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("assessment item insert failed", query);
    return {true, false, {}};
}

std::vector<Domain::AssessmentItem> SqlAssessmentRepository::itemsOf(
    const Domain::Uid &assessmentId)
{
    std::vector<Domain::AssessmentItem> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, item_type, prompt, reference_answer, rubric_json, source_refs_json, "
        "sequence_no, difficulty, revision FROM assessment_items_v4 "
        "WHERE assessment_id=(SELECT id FROM assessments_v4 WHERE uid=?) ORDER BY sequence_no"));
    query.addBindValue(QString::fromStdString(assessmentId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Domain::AssessmentItem item;
        item.uid = *uid;
        item.assessmentId = assessmentId;
        item.itemType = query.value("item_type").toString().toStdString();
        item.prompt = query.value("prompt").toString().toStdString();
        item.referenceAnswer = query.value("reference_answer").toString().toStdString();
        item.rubricJson = query.value("rubric_json").toString().toStdString();
        item.sourceRefsJson = query.value("source_refs_json").toString().toStdString();
        item.sequenceNo = query.value("sequence_no").toInt();
        if (!query.value("difficulty").isNull())
            item.difficulty = query.value("difficulty").toDouble();
        item.revision = query.value("revision").toInt();
        out.push_back(std::move(item));
    }
    return out;
}

Application::SaveResult SqlAssessmentRepository::insertAttempt(
    const Domain::AssessmentAttempt &attempt)
{
    const auto assessmentPk =
        resolvePk("SELECT id FROM assessments_v4 WHERE uid=?", attempt.assessmentId);
    if (!assessmentPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "assessment not found", {}, false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO assessment_attempts_v4(uid, assessment_id, started_at, submitted_at, "
        "answer_json, evidence_asset_uid, self_rating, idempotency_key) "
        "VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(attempt.uid.value()));
    query.addBindValue(*assessmentPk);
    query.addBindValue(QString::fromStdString(attempt.startedAt));
    query.addBindValue(attempt.submittedAt
                           ? QVariant(QString::fromStdString(*attempt.submittedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(attempt.answerJson));
    query.addBindValue(attempt.evidenceAssetUid
                           ? QVariant(QString::fromStdString(*attempt.evidenceAssetUid))
                           : QVariant());
    query.addBindValue(attempt.selfRating ? QVariant(*attempt.selfRating) : QVariant());
    query.addBindValue(QString::fromStdString(attempt.idempotencyKey));
    if (!query.exec())
        return writeFailure("assessment attempt insert failed", query);
    return {true, false, {}};
}

bool SqlAssessmentRepository::existsAttemptKey(const std::string &idempotencyKey)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM assessment_attempts_v4 WHERE idempotency_key=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    return query.exec() && query.next();
}

std::optional<Domain::AssessmentAttempt> SqlAssessmentRepository::findAttempt(
    const Domain::Uid &attemptId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT a.uid, x.uid AS assessment_uid, a.started_at, a.submitted_at, "
        "a.answer_json, a.evidence_asset_uid, a.self_rating, a.idempotency_key "
        "FROM assessment_attempts_v4 a "
        "LEFT JOIN assessments_v4 x ON x.id=a.assessment_id WHERE a.uid=?"));
    query.addBindValue(QString::fromStdString(attemptId.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    const auto assessmentUid =
        Domain::Uid::parse(query.value("assessment_uid").toString().toStdString());
    if (!uid || !assessmentUid)
        return std::nullopt;
    Domain::AssessmentAttempt attempt;
    attempt.uid = *uid;
    attempt.assessmentId = *assessmentUid;
    attempt.startedAt = query.value("started_at").toString().toStdString();
    if (!query.value("submitted_at").isNull())
        attempt.submittedAt = query.value("submitted_at").toString().toStdString();
    attempt.answerJson = query.value("answer_json").toString().toStdString();
    if (!query.value("evidence_asset_uid").isNull())
        attempt.evidenceAssetUid =
            query.value("evidence_asset_uid").toString().toStdString();
    if (!query.value("self_rating").isNull())
        attempt.selfRating = query.value("self_rating").toDouble();
    attempt.idempotencyKey = query.value("idempotency_key").toString().toStdString();
    return attempt;
}
std::vector<Domain::AssessmentAttempt> SqlAssessmentRepository::attemptsOf(
    const Domain::Uid &assessmentId)
{
    std::vector<Domain::AssessmentAttempt> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM assessment_attempts_v4 WHERE assessment_id="
        "(SELECT id FROM assessments_v4 WHERE uid=?) ORDER BY id"));
    query.addBindValue(QString::fromStdString(assessmentId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto attempt = findAttempt(*uid))
            out.push_back(*attempt);
    }
    return out;
}


Application::SaveResult SqlAssessmentRepository::insertResult(
    const Domain::AssessmentResult &result)
{
    const auto attemptPk =
        resolvePk("SELECT id FROM assessment_attempts_v4 WHERE uid=?", result.attemptId);
    if (!attemptPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "attempt not found", {}, false}};
    std::optional<qint64> itemPk;
    if (result.itemId) {
        itemPk = resolvePk("SELECT id FROM assessment_items_v4 WHERE uid=?", *result.itemId);
        if (!itemPk)
            return {false, false,
                    {Application::ErrorCode::NotFound, "item not found", {}, false}};
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO assessment_results_v4(uid, attempt_id, item_id, mastery, score, "
        "feedback, scorer, confidence, created_at, confirmed_by_user) "
        "VALUES(?,?,?,?,?,?,?,?,?,0)"));
    query.addBindValue(QString::fromStdString(result.uid.value()));
    query.addBindValue(*attemptPk);
    query.addBindValue(itemPk ? QVariant(*itemPk) : QVariant());
    query.addBindValue(QString::fromStdString(Domain::toString(result.mastery)));
    query.addBindValue(result.score ? QVariant(*result.score) : QVariant());
    query.addBindValue(result.feedback.empty()
                           ? QVariant()
                           : QVariant(QString::fromStdString(result.feedback)));
    query.addBindValue(QString::fromStdString(result.scorer));
    query.addBindValue(result.confidence);
    query.addBindValue(QString::fromStdString(result.createdAt));
    if (!query.exec())
        return writeFailure("assessment result insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAssessmentRepository::confirmResult(const Domain::Uid &resultUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE assessment_results_v4 SET confirmed_by_user=1 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(resultUid.value()));
    if (!query.exec())
        return writeFailure("assessment result confirm failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "result not found", {}, false}};
    return {true, false, {}};
}

std::vector<Domain::AssessmentResult> SqlAssessmentRepository::resultsOfAttempt(
    const Domain::Uid &attemptId)
{
    std::vector<Domain::AssessmentResult> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, mastery, score, feedback, scorer, confidence, created_at, "
        "confirmed_by_user FROM assessment_results_v4 "
        "WHERE attempt_id=(SELECT id FROM assessment_attempts_v4 WHERE uid=?) ORDER BY id"));
    query.addBindValue(QString::fromStdString(attemptId.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Domain::AssessmentResult result;
        result.uid = *uid;
        result.attemptId = attemptId;
        if (const auto mastery =
                Domain::masteryFrom(query.value("mastery").toString().toStdString()))
            result.mastery = *mastery;
        if (!query.value("score").isNull())
            result.score = query.value("score").toDouble();
        result.feedback = query.value("feedback").toString().toStdString();
        result.scorer = query.value("scorer").toString().toStdString();
        result.confidence = query.value("confidence").toDouble();
        result.createdAt = query.value("created_at").toString().toStdString();
        result.confirmedByUser = query.value("confirmed_by_user").toInt() != 0;
        out.push_back(std::move(result));
    }
    return out;
}

} // namespace PersonOS::Infrastructure
