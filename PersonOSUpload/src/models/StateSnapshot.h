#pragma once

#include <QString>

#include <optional>

// 状态快照（README 2.3.2-5 / 2.4.3）
// 对应表 state_snapshots，每日一条。State 描述"现在是什么状态"，
// 不含评价（事实与评价分离，2.3.5 原则3）。数值指标用 optional 表达"未填写"。
namespace PersonOS {

struct StateSnapshot
{
    qint64 id = 0;
    QString date;                    // YYYY-MM-DD
    std::optional<double> sleepHours;
    std::optional<int> energy;       // 1~5
    std::optional<int> focus;        // 1~5
    std::optional<int> mood;         // 1~5
    QString note;
    QString createdAt;
    QString updatedAt;
};

} // namespace PersonOS
