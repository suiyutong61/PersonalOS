#pragma once

#include <QString>

#include <optional>

// 任务（README 2.3.2-12 / 2.4.5）
// 对应表 tasks。状态集见 2.4.5.4；状态流转统一由 ExecutionService 控制，
// 仓库层不开放任意 UPDATE（3.3.2 写入纪律）。
namespace PersonOS {

struct Task
{
    qint64 id = 0;
    qint64 planId = 0;
    qint64 goalId = 0;                                // 任务→目标关联（FR-010），0 = 未关联
    QString title;
    QString description;
    std::optional<int> plannedMinutes;                // 计划分钟数
    QString dueDate;                                  // 所属日期 YYYY-MM-DD
    QString status = QStringLiteral("planned");       // planned/started/completed/partial/
                                                      // delayed/skipped/cancelled/interrupted
    std::optional<int> actualMinutes;                 // 实际分钟数
    int sortOrder = 0;
    QString createdAt;
    QString updatedAt;
    QString completedAt;

    bool isDone() const
    {
        return status == QStringLiteral("completed") || status == QStringLiteral("cancelled")
               || status == QStringLiteral("skipped");
    }
    bool isValid() const { return !title.trimmed().isEmpty(); }
};

} // namespace PersonOS
