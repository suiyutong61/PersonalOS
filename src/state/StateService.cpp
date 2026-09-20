#include "state/StateService.h"

#include <QDate>

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString todayString()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

bool scoreInRange(const std::optional<int> &v)
{
    return !v || (*v >= 1 && *v <= 5);
}

} // namespace

std::optional<StateSnapshot> StateService::getByDate(const QString &date) const
{
    return m_repo.getByDate(date);
}

std::optional<StateSnapshot> StateService::today() const
{
    return m_repo.getByDate(todayString());
}

bool StateService::validate(const StateSnapshot &s, QString *error) const
{
    if (s.date.trimmed().isEmpty()) {
        setError(error, QStringLiteral("日期不能为空"));
        return false;
    }
    if (s.sleepHours && (*s.sleepHours < 0.0 || *s.sleepHours > 24.0)) {
        setError(error, QStringLiteral("睡眠时长须在 0~24 小时之间（当前 %1）").arg(*s.sleepHours));
        return false;
    }
    if (!scoreInRange(s.energy)) {
        setError(error, QStringLiteral("精力评分须在 1~5 之间"));
        return false;
    }
    if (!scoreInRange(s.focus)) {
        setError(error, QStringLiteral("专注评分须在 1~5 之间"));
        return false;
    }
    if (!scoreInRange(s.mood)) {
        setError(error, QStringLiteral("心情评分须在 1~5 之间"));
        return false;
    }
    return true;
}

bool StateService::record(const StateSnapshot &s, QString *error)
{
    if (error)
        error->clear();
    if (!validate(s, error))
        return false;
    if (!m_repo.upsert(s)) {
        setError(error, m_repo.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
