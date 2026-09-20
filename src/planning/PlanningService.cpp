#include "planning/PlanningService.h"

#include <QDate>

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

bool PlanningService::validateDate(const QString &date, QString *error) const
{
    if (date.trimmed().isEmpty()) {
        setError(error, QStringLiteral("日期不能为空"));
        return false;
    }
    if (!QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid()) {
        setError(error, QStringLiteral("日期格式无效: %1（应为 YYYY-MM-DD）").arg(date));
        return false;
    }
    return true;
}

std::optional<Plan> PlanningService::dailyPlan(const QString &date) const
{
    return m_plans.getDaily(date);
}

Plan PlanningService::ensureDailyPlan(const QString &date, QString *error)
{
    if (error)
        error->clear();
    if (!validateDate(date, error))
        return {};
    const qint64 id = m_plans.ensureDaily(date);
    if (id == 0) {
        setError(error, m_plans.lastError());
        return {};
    }
    const auto plan = m_plans.getById(id);
    if (!plan) {
        setError(error, m_plans.lastError());
        return {};
    }
    return *plan;
}

qint64 PlanningService::addTaskToPlan(const QString &date, const Task &t, QString *error)
{
    if (error)
        error->clear();
    if (!validateDate(date, error))
        return 0;
    if (!t.isValid()) {
        setError(error, QStringLiteral("任务标题不能为空"));
        return 0;
    }
    const Plan plan = ensureDailyPlan(date, error);
    if (plan.id == 0)
        return 0;
    Task task = t;
    task.planId = plan.id;
    if (task.dueDate.isEmpty())
        task.dueDate = date;
    const qint64 id = m_tasks.create(task);
    if (id == 0)
        setError(error, m_tasks.lastError());
    return id;
}

bool PlanningService::closeDailyPlan(const QString &date, QString *error)
{
    if (error)
        error->clear();
    if (!validateDate(date, error))
        return false;
    const auto plan = m_plans.getDaily(date);
    if (!plan) {
        setError(error, QStringLiteral("当日计划不存在（%1），无法关闭").arg(date));
        return false;
    }
    if (!m_plans.setStatus(plan->id, QStringLiteral("closed"))) {
        setError(error, m_plans.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
