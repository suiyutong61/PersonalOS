#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/AssessmentRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/assessment/Assessment.h"
#include "domain/foundation/Clock.h"

// 验收用例（DD-001 §5.2 GenerateAssessment/ScoreAssessment；requirements R3.4）
// 纪律：单次验收不判定"完全掌握"；三档结果只描述本次表现；AI 评分需展示依据，
// 用户可复核/纠正；题目来源必须标注（AI 生成与真实原题区分）。
namespace PersonOS::Application {

class AssessmentUseCases
{
public:
    struct CreateInput
    {
        Domain::Uid userId;
        Domain::Uid goalId;
        std::optional<Domain::Uid> melId;
        std::string assessmentType;
        std::string scopeJson = "{}";
        std::string rubricJson = "{}";
        std::string generatedBy = "ai";
        std::optional<std::string> knowledgeSnapshotUid;
        std::vector<Domain::AssessmentItem> items;
    };

    struct CreateOutput
    {
        Domain::Assessment assessment;
    };

    AssessmentUseCases(AssessmentRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    Result<CreateOutput, ApplicationError> createAssessment(const CreateInput &input);

    // 发布验收（draft → ready）
    Result<Domain::Assessment, ApplicationError> readyAssessment(const Domain::Uid &assessmentUid,
                                                                 int expectedRevision);

    // 提交作答（ready/in_progress → in_progress；幂等键唯一）
    struct SubmitInput
    {
        std::string answerJson;
        std::optional<std::string> evidenceAssetUid;
        std::optional<double> selfRating;
        std::string idempotencyKey;   // 必填，唯一
    };
    Result<Domain::AssessmentAttempt, ApplicationError> submitAttempt(
        const Domain::Uid &assessmentUid, const SubmitInput &input);

    // 评分（AI 初评，用户可复核）：每条结果三档掌握；assessment → scored
    struct ScoreInput
    {
        Domain::Uid attemptId;
        std::string scorer;           // ai / user / rule / external
        struct ItemScore
        {
            Domain::Uid itemId;
            Domain::Mastery mastery;
            std::optional<double> score;
            std::string feedback;
        };
        std::vector<ItemScore> itemScores;
        double confidence = 1.0;
    };
    Result<std::vector<Domain::AssessmentResult>, ApplicationError> scoreAttempt(
        const Domain::Uid &assessmentUid, int expectedRevision, const ScoreInput &input);

    // 用户确认评分结果
    Result<Domain::AssessmentResult, ApplicationError> confirmResult(
        const Domain::Uid &resultUid);

private:
    AssessmentRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
