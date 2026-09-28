#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/review/Questionnaire.h"

// 复盘问卷仓储端口（DD-001 §5.3；数据库设计 §4.2）
namespace PersonOS::Application {

// 应答的三个区块：answers/no_change/skipped 各自引用的题目 code。
// 解析属于机械工作（JSON 读取），业务判定（去重、口径）在用例层完成。
struct ResponseSections
{
    std::vector<std::string> answers;
    std::vector<std::string> noChange;
    std::vector<std::string> skipped;
};

class QuestionnaireRepository
{
public:
    virtual ~QuestionnaireRepository() = default;

    virtual SaveResult insertQuestionnaire(const Domain::Questionnaire &questionnaire) = 0;
    virtual std::optional<Domain::Questionnaire> findByCodeVersion(
        const std::string &code, int versionNo) = 0;
    virtual std::optional<Domain::Questionnaire> findByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<std::string> itemCodesOf(const Domain::Uid &questionnaireUid) = 0;

    // 解析 responses_json；格式不合法返回 nullopt（用例层转 Validation 错误）
    virtual std::optional<ResponseSections> parseSections(
        const std::string &responsesJson) = 0;

    virtual SaveResult insertResponse(const Domain::QuestionnaireResponse &response) = 0;
    virtual SaveResult updateResponse(const Domain::QuestionnaireResponse &response) = 0;
    virtual std::optional<Domain::QuestionnaireResponse> findResponse(
        const Domain::Uid &reviewUid, const Domain::Uid &questionnaireUid) = 0;
};

} // namespace PersonOS::Application
