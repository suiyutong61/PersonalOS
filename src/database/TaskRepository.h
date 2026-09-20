#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Task.h"

namespace PersonOS {

// 任务仓库（README 3.3.4）。提供读写原语；
// 状态流转统一由 ExecutionService 控制（3.3.2 写入纪律）。
class TaskRepository
{
public:
    std::optional<Task> getById(qint64 id) const;
    std::vector<Task> getByDate(const QString &date) const; // ORDER BY sort_order, id
    std::vector<Task> getByPlan(qint64 planId) const;
    qint64 create(const Task &t);                   // 返回新 id；失败返回 0
    bool update(const Task &t);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
