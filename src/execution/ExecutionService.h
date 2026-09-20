#pragma once

#include <QString>

#include "database/EventRepository.h"
#include "database/TaskRepository.h"
#include "models/Task.h"

namespace PersonOS {

// Execution 子系统（README 2.4.5 / 3.3.5）
// 职责：任务状态流转（2.4.5.4）。每次流转自动向事件流追加一条 Event（2.5.2）。
// 状态流转是任务状态唯一的变更通道；updateTask 只允许编辑非状态字段。
class ExecutionService
{
public:
    bool startTask(qint64 taskId, QString *error = nullptr);
    bool completeTask(qint64 taskId, int actualMinutes, QString *error = nullptr);
    bool skipTask(qint64 taskId, const QString &reason, QString *error = nullptr);
    bool cancelTask(qint64 taskId, const QString &reason, QString *error = nullptr);

    // 编辑任务字段（标题/描述/时长/日期/顺序），不允许改变状态（3.3.2 写入纪律）
    bool updateTask(const Task &t, QString *error = nullptr);

private:
    // 通用流转：存在性检查 + 终态检查（completed/cancelled/skipped 不可再流转）
    bool transition(qint64 taskId, const QString &newStatus, QString *error);
    void logEvent(const Task &t, const QString &type, const QString &title,
                  const QString &description = {});

    TaskRepository m_tasks;
    EventRepository m_events;
};

} // namespace PersonOS
