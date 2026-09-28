#include "application/usecases/state/CalibrationUseCases.h"

#include <iomanip>
#include <sstream>

namespace PersonOS::Application {

namespace {

ApplicationError validation(const std::string &message)
{
    return {ErrorCode::Validation, message, {}, false};
}

// 轻量 JSON 结构检查（完整校验由数据库 json_valid 约束兜底）
bool looksLikeJsonObject(const std::string &text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n");
    return first != std::string::npos && last != std::string::npos && text[first] == '{'
           && text[last] == '}';
}

// 足够精度保证 JSON 文本往返后双精度数值不变（不向用户伪装为更精确）
std::string doubleText(double value)
{
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

CalibrationUseCases::CalibrationUseCases(CalibrationRepository &repo, UuidPort &uids,
                                         const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<Domain::CalibrationRecord, ApplicationError> CalibrationUseCases::record(
    const RecordInput &input)
{
    if (input.userId.empty() || input.parameterCode.empty())
        return Result<Domain::CalibrationRecord, ApplicationError>::failure(
            validation("userId and parameterCode required"));
    if (!looksLikeJsonObject(input.newValueJson) || !looksLikeJsonObject(input.evidenceJson))
        return Result<Domain::CalibrationRecord, ApplicationError>::failure(
            validation("newValueJson/evidenceJson must be JSON objects"));
    if (input.oldValueJson && !looksLikeJsonObject(*input.oldValueJson))
        return Result<Domain::CalibrationRecord, ApplicationError>::failure(
            validation("oldValueJson must be a JSON object"));

    Domain::CalibrationRecord record;
    record.uid = m_uids.next();
    record.userId = input.userId;
    record.domainManifestId = input.domainManifestId;
    record.parameterCode = input.parameterCode;
    record.oldValueJson = input.oldValueJson;
    record.newValueJson = input.newValueJson;
    record.evidenceJson = input.evidenceJson;
    record.effectiveFrom = m_clock.utcIso();
    record.createdAt = record.effectiveFrom;
    record.decisionUid = input.decisionUid;
    if (!record.isValid())
        return Result<Domain::CalibrationRecord, ApplicationError>::failure(
            validation("calibration record invalid"));

    const auto saved = m_repo.insert(record);
    if (!saved.ok)
        return Result<Domain::CalibrationRecord, ApplicationError>::failure(saved.error);
    return Result<Domain::CalibrationRecord, ApplicationError>::success(std::move(record));
}

Result<std::optional<Domain::CalibrationRecord>, ApplicationError>
CalibrationUseCases::activeValue(const Domain::Uid &userId, const std::string &parameterCode)
{
    if (userId.empty() || parameterCode.empty())
        return Result<std::optional<Domain::CalibrationRecord>, ApplicationError>::failure(
            validation("userId and parameterCode required"));
    return Result<std::optional<Domain::CalibrationRecord>, ApplicationError>::success(
        m_repo.activeValue(userId, parameterCode));
}

Result<CalibrationUseCases::MelOutcomeOutput, ApplicationError>
CalibrationUseCases::recordMelOutcome(const Domain::Uid &melUid)
{
    if (melUid.empty())
        return Result<MelOutcomeOutput, ApplicationError>::failure(
            validation("mel uid required"));

    const Domain::MelOutcomeFacts facts = m_repo.melOutcomeFacts(melUid);
    if (!facts.melExists)
        return Result<MelOutcomeOutput, ApplicationError>::failure(
            {ErrorCode::NotFound, "mel not found", {}, false});

    // 无预测记录时不产生校准：样本不足，诚实不记录（不伪造基线）
    if (!facts.hasPrediction)
        return Result<MelOutcomeOutput, ApplicationError>::success(
            MelOutcomeOutput{false, "no prediction to calibrate against", {}});

    // 完成度口径：按计划工作量加权；全部为零时退化为等权平均，并在证据中说明
    double actualCompletion = 0.0;
    std::string completionCaliber;
    if (facts.plannedEffortSum > 0 && facts.requiredTaskCount > 0) {
        actualCompletion = facts.weightedProgressSum / static_cast<double>(facts.plannedEffortSum);
        completionCaliber = "weighted by planned_effort_min over required tasks";
    } else if (facts.requiredTaskCount > 0) {
        actualCompletion = facts.taskProgressSum / static_cast<double>(facts.requiredTaskCount);
        completionCaliber = "equal weight (all planned_effort_min zero)";
    } else {
        completionCaliber = "no required tasks";
    }

    const std::string melUidText = melUid.value();
    MelOutcomeOutput output;
    output.recorded = true;

    // 参数 1：完成率预测 vs 实际
    {
        RecordInput input;
        input.userId = facts.userId;
        input.parameterCode = "mel_completion_ratio";
        input.oldValueJson = std::string("{\"predicted\":") + doubleText(facts.predictedCompletion)
                             + "}";
        input.newValueJson = std::string("{\"actual\":") + doubleText(actualCompletion) + "}";
        input.evidenceJson = std::string("{\"mel_uid\":\"") + melUidText
                             + "\",\"required_task_count\":" + std::to_string(facts.requiredTaskCount)
                             + ",\"caliber\":\"" + completionCaliber + "\"}";
        const auto recorded = record(input);
        if (!recorded)
            return Result<MelOutcomeOutput, ApplicationError>::failure(recorded.error());
        output.records.push_back(recorded.value());
    }

    // 参数 2：耗时预测 vs 实际（分钟）
    {
        RecordInput input;
        input.userId = facts.userId;
        input.parameterCode = "mel_effort_ratio";
        input.oldValueJson = std::string("{\"predicted_effort_min\":")
                             + std::to_string(facts.predictedEffortMin) + "}";
        input.newValueJson = std::string("{\"actual_minutes\":")
                             + std::to_string(facts.actualMinutesSum) + "}";
        input.evidenceJson = std::string("{\"mel_uid\":\"") + melUidText
                             + "\",\"planned_minutes\":"
                             + std::to_string(facts.plannedEffortSum) + ",\"unit\":\"minutes\"}";
        const auto recorded = record(input);
        if (!recorded)
            return Result<MelOutcomeOutput, ApplicationError>::failure(recorded.error());
        output.records.push_back(recorded.value());
    }

    output.reason.clear();
    return Result<MelOutcomeOutput, ApplicationError>::success(std::move(output));
}

} // namespace PersonOS::Application
