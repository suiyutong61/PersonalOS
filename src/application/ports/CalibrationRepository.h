#pragma once

#include <optional>
#include <string>

#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/state/Calibration.h"

// 个人校准仓储端口（DD-001 §5.3；数据库设计 §4.3）
namespace PersonOS::Application {

class CalibrationRepository
{
public:
    virtual ~CalibrationRepository() = default;

    virtual SaveResult insert(const Domain::CalibrationRecord &record) = 0;

    // 某参数的最新有效校准值（按 effective_from、id 降序取第一条）；无则 nullopt
    virtual std::optional<Domain::CalibrationRecord> activeValue(
        const Domain::Uid &userId, const std::string &parameterCode) = 0;

    // MEL 预测与实际事实（SQL 汇总；口径判定在用例层）
    virtual Domain::MelOutcomeFacts melOutcomeFacts(const Domain::Uid &melUid) = 0;
};

} // namespace PersonOS::Application
