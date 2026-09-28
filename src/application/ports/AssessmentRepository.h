#pragma once

#include <optional>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/assessment/Assessment.h"
#include "domain/foundation/Uid.h"

// 验收仓储端口（DD-001 §5.3）
namespace PersonOS::Application {

class AssessmentRepository
{
public:
    virtual ~AssessmentRepository() = default;

    virtual std::optional<Domain::Assessment> findByUid(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::Assessment &assessment) = 0;
    virtual SaveResult update(const Domain::Assessment &assessment, int expectedRevision) = 0;
    // 验收页只读投影（最近的验收记录）
    virtual std::vector<Domain::Assessment> listForUser(const Domain::Uid &userId,
                                                        int limit) = 0;

    virtual SaveResult insertItem(const Domain::AssessmentItem &item) = 0;
    virtual std::vector<Domain::AssessmentItem> itemsOf(const Domain::Uid &assessmentId) = 0;

    virtual SaveResult insertAttempt(const Domain::AssessmentAttempt &attempt) = 0;
    virtual bool existsAttemptKey(const std::string &idempotencyKey) = 0;
    virtual std::optional<Domain::AssessmentAttempt> findAttempt(const Domain::Uid &attemptId) = 0;
    virtual std::vector<Domain::AssessmentAttempt> attemptsOf(const Domain::Uid &assessmentId) = 0;

    virtual SaveResult insertResult(const Domain::AssessmentResult &result) = 0;
    virtual SaveResult confirmResult(const Domain::Uid &resultUid) = 0;
    virtual std::vector<Domain::AssessmentResult> resultsOfAttempt(
        const Domain::Uid &attemptId) = 0;
};

} // namespace PersonOS::Application
