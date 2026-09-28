#pragma once

#include <QSqlDatabase>

#include "application/ports/AssessmentRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlAssessmentRepository final : public Application::AssessmentRepository
{
public:
    explicit SqlAssessmentRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::Assessment> findByUid(const Domain::Uid &uid) override;
    std::vector<Domain::Assessment> listForUser(const Domain::Uid &userId, int limit) override;
    Application::SaveResult insert(const Domain::Assessment &assessment) override;
    Application::SaveResult update(const Domain::Assessment &assessment,
                                   int expectedRevision) override;

    Application::SaveResult insertItem(const Domain::AssessmentItem &item) override;
    std::vector<Domain::AssessmentItem> itemsOf(const Domain::Uid &assessmentId) override;

    Application::SaveResult insertAttempt(const Domain::AssessmentAttempt &attempt) override;
    bool existsAttemptKey(const std::string &idempotencyKey) override;
    std::optional<Domain::AssessmentAttempt> findAttempt(const Domain::Uid &attemptId) override;
    std::vector<Domain::AssessmentAttempt> attemptsOf(const Domain::Uid &assessmentId) override;

    Application::SaveResult insertResult(const Domain::AssessmentResult &result) override;
    Application::SaveResult confirmResult(const Domain::Uid &resultUid) override;
    std::vector<Domain::AssessmentResult> resultsOfAttempt(
        const Domain::Uid &attemptId) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;
    std::optional<qint64> resolvePk(const char *sql, const Domain::Uid &uid) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
