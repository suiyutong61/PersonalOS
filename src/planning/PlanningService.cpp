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
    return m_repo.getDaily(date);
}

Plan PlanningService::ensureDailyPlan(const QString &date, QString *error)
{
    if (error)
        error->clear();
    if (!validateDate(date, error))
        return {};
    const qint64 id = m_repo.ensureDaily(date);
    if (id == 0) {
        setError(error, m_repo.lastError());
        return {};
    }
    const auto plan = m_repo.getById(id);
    if (!plan) {
        setError(error, m_repo.lastError());
        return {};
    }
    return *plan;
}

bool PlanningService::closeDailyPlan(const QString &date, QString *error)
{
    if (error)
        error->clear();
    if (!validateDate(date, error))
        return false;
    const auto plan = m_repo.getDaily(date);
    if (!plan) {
        setError(error, QStringLiteral("当日计划不存在（%1），无法关闭").arg(date));
        return false;
    }
    if (!m_repo.setStatus(plan->id, QStringLiteral("closed"))) {
        setError(error, m_repo.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
