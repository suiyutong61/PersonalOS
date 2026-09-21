#pragma once

#include <optional>

#include <QString>

#include "database/StateRepository.h"
#include "models/StateSnapshot.h"

namespace PersonOS {

// Personal State 子系统（README 2.4.3 / 3.3.5）
// 职责：每日状态快照的记录与读取。
// State 只描述"现在是什么状态"，不含评价（2.3.5 原则3）。
class StateService
{
public:
    std::optional<StateSnapshot> getByDate(const QString &date) const; // YYYY-MM-DD
    std::optional<StateSnapshot> today() const;

    // 记录/更新当日状态（校验：睡眠 0~24 小时，精力/专注/心情 1~5）
    bool record(const StateSnapshot &s, QString *error = nullptr);

private:
    bool validate(const StateSnapshot &s, QString *error) const;

    StateRepository m_repo;
};

} // namespace PersonOS
