#pragma once

#include <QSqlDatabase>

#include "application/ports/QuestionnaireRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlQuestionnaireRepository final : public Application::QuestionnaireRepository
{
public:
    explicit SqlQuestionnaireRepository(QSqlDatabase database, const Domain::Clock &clock);

    Application::SaveResult insertQuestionnaire(
        const Domain::Questionnaire &questionnaire) override;
    std::optional<Domain::Questionnaire> findByCodeVersion(const std::string &code,
                                                           int versionNo) override;
    std::optional<Domain::Questionnaire> findByUid(const Domain::Uid &uid) override;
    std::vector<std::string> itemCodesOf(const Domain::Uid &questionnaireUid) override;

    std::optional<Application::ResponseSections> parseSections(
        const std::string &responsesJson) override;

    Application::SaveResult insertResponse(
        const Domain::QuestionnaireResponse &response) override;
    Application::SaveResult updateResponse(
        const Domain::QuestionnaireResponse &response) override;
    std::optional<Domain::QuestionnaireResponse> findResponse(
        const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
