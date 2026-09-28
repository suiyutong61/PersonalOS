#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/CalibrationRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/state/Calibration.h"

// 个人校准用例（架构 4.3.3.1、DR-036；domain-configuration-design-v1.md §4/§5）
// 纪律：预测→实际→校准记录只追加；样本不足时保留不确定性；
// 校准只调整个人估时/方法适配/候选优先级，不训练模型、不改写论文结论；
// 无预测记录时不产生校准（诚实记录原因），不伪造样本。
namespace PersonOS::Application {

class CalibrationUseCases
{
public:
    struct RecordInput
    {
        Domain::Uid userId;
        std::string parameterCode;
        std::optional<std::string> oldValueJson;   // 预测值
        std::string newValueJson;                  // 实际值（含口径说明）
        std::string evidenceJson;                  // 样本、口径与依据
        std::optional<Domain::Uid> domainManifestId;
        std::optional<std::string> decisionUid;
    };

    struct MelOutcomeOutput
    {
        bool recorded = false;                     // 无预测时保持 false
        std::string reason;                        // 未记录原因（如有）
        std::vector<Domain::CalibrationRecord> records;
    };

    CalibrationUseCases(CalibrationRepository &repo, UuidPort &uids,
                        const Domain::Clock &clock);

    // 手动/上游追加校准记录（只追加）
    Result<Domain::CalibrationRecord, ApplicationError> record(const RecordInput &input);

    // 最新有效校准值（下一轮计划生成前必须实际检索，不依赖模型"记得"用户）
    Result<std::optional<Domain::CalibrationRecord>, ApplicationError> activeValue(
        const Domain::Uid &userId, const std::string &parameterCode);

    // MEL 预测 vs 实际的最小闭环：读取预测与执行事实，追加 mel_completion_ratio
    // 与 mel_effort_ratio 两条校准记录；无预测时不产生记录
    Result<MelOutcomeOutput, ApplicationError> recordMelOutcome(const Domain::Uid &melUid);

private:
    CalibrationRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
