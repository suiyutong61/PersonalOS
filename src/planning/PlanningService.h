#pragma once

#include <optional>

#include <QString>

#include "database/PlanRepository.h"
#include "database/TaskRepository.h"
#include "models/Plan.h"
#include "models/Task.h"

namespace PersonOS {

// Planning 子系统（README 2.4.4 / 3.3.5）
// 职责：日计划的创建与生命周期管理、任务挂入计划。
// 计划 ≠ 目标：本子系统只调整计划，绝不修改目标（2.2.6）。
class PlanningService
{
public:
    std::optional<Plan> dailyPlan(const QString &date) const; // YYYY-MM-DD

    // 应用启动 / 跨日时调用（3.0.3 循环触发时机）；幂等：同一天只创建一个计划。
    // 返回带 id 的 Plan；失败时 id=0 且 error 非空。
    Plan ensureDailyPlan(const QString &date, QString *error = nullptr);

    // 把任务挂入当日计划（自动 ensure 计划）；返回新任务 id，失败 0
    qint64 addTaskToPlan(const QString &date, const Task &t, QString *error = nullptr);

    // 日终复盘完成后关闭当日计划（status: active → closed）
    bool closeDailyPlan(const QString &date, QString *error = nullptr);

    // 注：reschedule（日间动态调整，2.5.5）= 经 ExecutionService::updateTask
    // 修改任务的 due_date/sort_order，MVP 不单独实现。

private:
    bool validateDate(const QString &date, QString *error) const;

    PlanRepository m_plans;
    TaskRepository m_tasks;
};

} // namespace PersonOS
