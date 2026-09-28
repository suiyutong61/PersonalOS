#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/ReviewRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/review/Review.h"

// 复盘用例（DD-001 §5.2 SubmitReviewState/CloseReview；requirements R3.5/R3.6）
namespace PersonOS::Application {

class ReviewUseCases
{
public:
    ReviewUseCases(ReviewRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    // 结算时开启复盘（每个 MEL 至多一个，UNIQUE 约束兜底）
    Result<Domain::Review, ApplicationError> openReview(const Domain::Uid &melUid);

    // 用户提交复盘内容（状态 collecting/assessing/decision → confirmed）
    struct SubmitInput
    {
        std::string summary;            // 完成比例、耗时偏差、验收结果、主要问题
        std::string userComment;
        std::string nextAction;         // 新 MEL / 暂停 / 改路线 / 结束目标
        std::optional<std::string> knowledgeSnapshotUid;
    };
    Result<Domain::Review, ApplicationError> submitReview(const Domain::Uid &melUid,
                                                          int expectedRevision,
                                                          const SubmitInput &input);

    // 复盘关闭（配合 MEL CloseReview）
    Result<Domain::Review, ApplicationError> closeReview(const Domain::Uid &melUid,
                                                         int expectedRevision);

private:
    ReviewRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
