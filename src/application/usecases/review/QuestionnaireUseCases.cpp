#include "application/usecases/review/QuestionnaireUseCases.h"

#include "application/audit/Audit.h"

#include <algorithm>

namespace PersonOS::Application {

namespace {

ApplicationError validation(const std::string &message)
{
    return {ErrorCode::Validation, message, {}, false};
}

bool contains(const std::vector<std::string> &values, const std::string &value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

QuestionnaireUseCases::QuestionnaireUseCases(QuestionnaireRepository &repo,
                                             ReviewRepository &reviews, UuidPort &uids,
                                             const Domain::Clock &clock)
    : m_repo(repo), m_reviews(reviews), m_uids(uids), m_clock(clock)
{}

Result<Domain::Questionnaire, ApplicationError> QuestionnaireUseCases::registerQuestionnaire(
    const RegisterInput &input)
{
    if (input.code.empty() || input.name.empty() || input.versionNo <= 0)
        return Result<Domain::Questionnaire, ApplicationError>::failure(
            validation("questionnaire code/name/version required"));
    if (m_repo.findByCodeVersion(input.code, input.versionNo))
        return Result<Domain::Questionnaire, ApplicationError>::failure(
            {ErrorCode::Conflict, "questionnaire version already exists", {}, false});

    Domain::Questionnaire questionnaire;
    questionnaire.uid = m_uids.next();
    questionnaire.code = input.code;
    questionnaire.name = input.name;
    questionnaire.versionNo = input.versionNo;
    questionnaire.schemaJson = input.schemaJson;
    questionnaire.status = "active";
    questionnaire.createdAt = m_clock.utcIso();
    questionnaire.updatedAt = questionnaire.createdAt;
    if (!questionnaire.isValid())
        return Result<Domain::Questionnaire, ApplicationError>::failure(
            validation("questionnaire invalid"));

    const auto saved = m_repo.insertQuestionnaire(questionnaire);
    if (!saved.ok)
        return Result<Domain::Questionnaire, ApplicationError>::failure(saved.error);
    return Result<Domain::Questionnaire, ApplicationError>::success(std::move(questionnaire));
}

Result<Domain::QuestionnaireResponse, ApplicationError>
QuestionnaireUseCases::submitResponse(const Domain::Uid &reviewUid,
                                      const Domain::Uid &questionnaireUid,
                                      const SubmitInput &input)
{
    const auto review = m_reviews.findByUid(reviewUid);
    if (!review)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            {ErrorCode::NotFound, "review not found", {}, false});
    if (review->status == Domain::ReviewStatus::Draft
        || review->status == Domain::ReviewStatus::Closed)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            {ErrorCode::Conflict, "review is not open for questionnaire", {}, false});

    const auto questionnaire = m_repo.findByUid(questionnaireUid);
    if (!questionnaire)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            {ErrorCode::NotFound, "questionnaire not found", {}, false});

    // 解析应答区块；格式不合法 → Validation（跳过/未回答不视为确认）
    const auto sections = m_repo.parseSections(input.responsesJson);
    if (!sections)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            validation("responses json invalid: expect {answers,no_change,skipped}"));

    const auto itemCodes = m_repo.itemCodesOf(questionnaireUid);
    if (itemCodes.empty())
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            validation("questionnaire schema has no items"));

    // 题目必须存在；三区块互斥
    for (const auto &code : sections->answers)
        if (!contains(itemCodes, code))
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                validation("answer references unknown item: " + code));
    for (const auto &code : sections->noChange)
        if (!contains(itemCodes, code))
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                validation("no_change references unknown item: " + code));
    for (const auto &code : sections->skipped)
        if (!contains(itemCodes, code))
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                validation("skipped references unknown item: " + code));
    for (const auto &code : sections->answers)
        if (contains(sections->noChange, code) || contains(sections->skipped, code))
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                validation("item appears in multiple sections: " + code));
    for (const auto &code : sections->noChange)
        if (contains(sections->skipped, code))
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                validation("item appears in multiple sections: " + code));

    // 完成比例：answers + no_change 计入，skipped 不计入（可跳过的部分问卷）
    const double total = static_cast<double>(itemCodes.size());
    const double done =
        static_cast<double>(sections->answers.size() + sections->noChange.size());
    const double completionRatio = std::clamp(done / total, 0.0, 1.0);

    const auto existing = m_repo.findResponse(reviewUid, questionnaireUid);
    const std::string now = m_clock.utcIso();
    if (existing) {
        // 可修改：更新同一行
        Domain::QuestionnaireResponse updated = *existing;
        updated.responsesJson = input.responsesJson;
        updated.submittedAt = now;
        updated.completionRatio = completionRatio;
        const auto saved = m_repo.updateResponse(updated);
        if (!saved.ok)
            return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
                saved.error);
        Audit::record({"user", {}, "review.questionnaire_submitted", "review",
                       reviewUid.value(),
                       "{\"completion\":" + std::to_string(completionRatio) + "}"});
        return Result<Domain::QuestionnaireResponse, ApplicationError>::success(
            std::move(updated));
    }

    Domain::QuestionnaireResponse response;
    response.uid = m_uids.next();
    response.reviewId = reviewUid;
    response.questionnaireId = questionnaireUid;
    response.responsesJson = input.responsesJson;
    response.startedAt = now;
    response.submittedAt = now;
    response.completionRatio = completionRatio;
    const auto saved = m_repo.insertResponse(response);
    if (!saved.ok)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "review.questionnaire_submitted", "review",
                   reviewUid.value(),
                   "{\"completion\":" + std::to_string(completionRatio) + "}"});
    return Result<Domain::QuestionnaireResponse, ApplicationError>::success(
        std::move(response));
}

Result<Domain::QuestionnaireResponse, ApplicationError> QuestionnaireUseCases::responseOf(
    const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid)
{
    const auto response = m_repo.findResponse(reviewUid, questionnaireUid);
    if (!response)
        return Result<Domain::QuestionnaireResponse, ApplicationError>::failure(
            {ErrorCode::NotFound, "questionnaire response not found", {}, false});
    return Result<Domain::QuestionnaireResponse, ApplicationError>::success(*response);
}

Result<Domain::Questionnaire, ApplicationError> QuestionnaireUseCases::findByCodeVersion(
    const std::string &code, int versionNo)
{
    const auto questionnaire = m_repo.findByCodeVersion(code, versionNo);
    if (!questionnaire)
        return Result<Domain::Questionnaire, ApplicationError>::failure(
            {ErrorCode::NotFound, "questionnaire not found", {}, false});
    return Result<Domain::Questionnaire, ApplicationError>::success(*questionnaire);
}

} // namespace PersonOS::Application
