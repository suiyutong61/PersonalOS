#pragma once

#include <optional>
#include <string>

#include "domain/foundation/Uid.h"

// 个人校准记录（架构 4.3.3.1、DR-036；domain-configuration-design-v1.md §4；
// 数据库设计 §4.3 calibration_records_v4）
// 纪律：只追加、不覆盖；只调整个人估时/方法适配/候选优先级，不训练外部模型、
// 不改写通用论文结论；一次波动不永久改变长期参数。
namespace PersonOS::Domain {

struct CalibrationRecord
{
    Uid uid;
    Uid userId;
    std::optional<Uid> domainManifestId;
    std::string parameterCode;          // 如 mel_completion_ratio / mel_effort_ratio
    std::optional<std::string> oldValueJson;   // 预测值
    std::string newValueJson;                 // 实际值（含口径说明）
    std::string evidenceJson;                 // 样本、计算口径与依据
    std::string effectiveFrom;                // UTC ISO-8601
    std::string createdAt;
    std::optional<std::string> decisionUid;

    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !parameterCode.empty()
               && !newValueJson.empty() && !evidenceJson.empty() && !effectiveFrom.empty();
    }
};

// 校准输入事实：仓储聚合预测与实际（SQL 汇总），用例层计算口径与判定。
struct MelOutcomeFacts
{
    bool melExists = false;
    Domain::Uid userId;                 // mels_v4.user_id
    bool hasPrediction = false;
    double predictedCompletion = 0.0;   // mel_predictions_v4 最新未废弃行
    int predictedEffortMin = 0;
    int requiredTaskCount = 0;          // mel_tasks_v4 required=1
    double taskProgressSum = 0.0;       // Σ progress
    double weightedProgressSum = 0.0;   // Σ progress × planned_effort_min
    int plannedEffortSum = 0;           // Σ planned_effort_min
    int actualMinutesSum = 0;           // progress_events_v4 unit='minutes' 的 Σ amount
};

} // namespace PersonOS::Domain
