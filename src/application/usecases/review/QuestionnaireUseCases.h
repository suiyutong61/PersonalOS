#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/QuestionnaireRepository.h"
#include "application/ports/ReviewRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/review/Questionnaire.h"

// 复盘问卷用例（requirements R3.5/R4.3.3；DD-001 §5.2 SubmitReviewState 的一部分）
// 纪律：问卷可跳过、可修改（重复提交更新同一行）；跳过/未回答不视为确认，
// 只有显式 no_change 才算一次"无明显变化"确认；完成比例 = (answers+no_change)/总数。
namespace PersonOS::Application {

class QuestionnaireUseCases
{
public:
    struct RegisterInput
    {
        std::string code;
        std::string name;
        int versionNo = 0;             // >0；改版产生新行
        std::string schemaJson = "{}"; // {"items":[{"code":...},...]}
    };

    struct SubmitInput
    {
        // {"answers":{code:value},"no_change":[code],"skipped":[code]}；
        // 格式由仓储解析校验，业务判定（题目存在、区块互斥）在本用例完成
        std::string responsesJson;
    };

    QuestionnaireUseCases(QuestionnaireRepository &repo, ReviewRepository &reviews,
                          UuidPort &uids, const Domain::Clock &clock);

    // 注册问卷（UNIQUE(code,version_no)；已存在返回 Conflict）
    Result<Domain::Questionnaire, ApplicationError> registerQuestionnaire(
        const RegisterInput &input);

    // 提交/更新复盘问卷应答；复盘已关闭时拒绝
    Result<Domain::QuestionnaireResponse, ApplicationError> submitResponse(
        const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid,
        const SubmitInput &input);

    Result<Domain::QuestionnaireResponse, ApplicationError> responseOf(
        const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid);

    Result<Domain::Questionnaire, ApplicationError> findByCodeVersion(const std::string &code,
                                                                      int versionNo);

private:
    QuestionnaireRepository &m_repo;
    ReviewRepository &m_reviews;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
