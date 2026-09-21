#pragma once

#include <QString>

// 事件（README 2.3.2-14 / 2.4.6）
// 对应表 events。append-only：仓库层只提供 INSERT/SELECT，
// 修正走留痕流程（2.4.14 原则1）。
namespace PersonOS {

struct Event
{
    qint64 id = 0;
    QString date;                    // YYYY-MM-DD
    QString occurredAt;              // 实际发生时间（本地时间）
    QString type;                    // task_completed/task_skipped/interruption/
                                     // state_recorded/review_done/custom ...
    QString title;
    QString description;
    qint64 taskId = 0;               // 关联任务，0 = 无
    QString createdAt;
};

} // namespace PersonOS
