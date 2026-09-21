#include "execution/ExecutionService.h"

#include <QDate>
#include <QDateTime>

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString nowString()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString todayString()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

void ExecutionService::logEvent(const Task &t, const QString &type, const QString &title,
                                const QString &description)
{
    Event e;
    // 事件属于"行动发生的日期"（今天），任务计划日期在 t.dueDate；
    // 复盘时经 task_id 关联（2.5.2 数据流）
    e.date = todayString();
    e.occurredAt = nowString();
    e.type = type;
    e.title = title;
    e.description = description;
    e.taskId = t.id;
    m_events.append(e);
}

bool ExecutionService::transition(qint64 taskId, const QString &newStatus, QString *error)
{
    if (error)
        error->clear();
    const auto existing = m_tasks.getById(taskId);
    if (!existing) {
        setError(error, QStringLiteral("任务不存在（id=%1）").arg(taskId));
        return false;
    }
    if (existing->isDone()) {
        setError(error, QStringLiteral("任务已终结（%1），不能再流转").arg(existing->status));
        return false;
    }
    Task t = *existing;
    t.status = newStatus;
    if (!m_tasks.update(t)) {
        setError(error, m_tasks.lastError());
        return false;
    }
    return true;
}

bool ExecutionService::startTask(qint64 taskId, QString *error)
{
    if (!transition(taskId, QStringLiteral("started"), error))
        return false;
    logEvent(*m_tasks.getById(taskId), QStringLiteral("task_started"),
             QStringLiteral("任务开始"));
    return true;
}

bool ExecutionService::completeTask(qint64 taskId, int actualMinutes, QString *error)
{
    if (error)
        error->clear();
    if (actualMinutes < 0) {
        setError(error, QStringLiteral("实际时长不能为负数"));
        return false;
    }
    const auto existing = m_tasks.getById(taskId);
    if (!existing) {
        setError(error, QStringLiteral("任务不存在（id=%1）").arg(taskId));
        return false;
    }
    if (existing->isDone()) {
        setError(error, QStringLiteral("任务已终结（%1），不能重复完成").arg(existing->status));
        return false;
    }
    Task t = *existing;
    t.status = QStringLiteral("completed");
    t.actualMinutes = actualMinutes;
    t.completedAt = nowString();
    if (!m_tasks.update(t)) {
        setError(error, m_tasks.lastError());
        return false;
    }
    logEvent(t, QStringLiteral("task_completed"),
             QStringLiteral("任务完成（实际 %1 分钟）").arg(actualMinutes));
    return true;
}

bool ExecutionService::skipTask(qint64 taskId, const QString &reason, QString *error)
{
    if (!transition(taskId, QStringLiteral("skipped"), error))
        return false;
    logEvent(*m_tasks.getById(taskId), QStringLiteral("task_skipped"),
             QStringLiteral("任务跳过"), reason);
    return true;
}

bool ExecutionService::cancelTask(qint64 taskId, const QString &reason, QString *error)
{
    if (!transition(taskId, QStringLiteral("cancelled"), error))
        return false;
    logEvent(*m_tasks.getById(taskId), QStringLiteral("task_cancelled"),
             QStringLiteral("任务取消"), reason);
    return true;
}

bool ExecutionService::updateTask(const Task &t, QString *error)
{
    if (error)
        error->clear();
    if (!t.isValid()) {
        setError(error, QStringLiteral("任务标题不能为空"));
        return false;
    }
    const auto existing = m_tasks.getById(t.id);
    if (!existing) {
        setError(error, QStringLiteral("任务不存在（id=%1）").arg(t.id));
        return false;
    }
    if (existing->status != t.status) {
        setError(error, QStringLiteral("updateTask 不允许改变任务状态（当前 %1），请使用状态流转方法")
                            .arg(existing->status));
        return false;
    }
    if (!m_tasks.update(t)) {
        setError(error, m_tasks.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
