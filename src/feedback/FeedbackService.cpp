#include "feedback/FeedbackService.h"

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

bool FeedbackService::validate(const Event &e, QString *error) const
{
    if (e.date.trimmed().isEmpty()) {
        setError(error, QStringLiteral("事件日期不能为空"));
        return false;
    }
    if (e.type.trimmed().isEmpty()) {
        setError(error, QStringLiteral("事件类型不能为空"));
        return false;
    }
    if (e.title.trimmed().isEmpty()) {
        setError(error, QStringLiteral("事件标题不能为空"));
        return false;
    }
    return true;
}

qint64 FeedbackService::recordEvent(const Event &e, QString *error)
{
    if (error)
        error->clear();
    if (!validate(e, error))
        return 0;
    const qint64 id = m_events.append(e);
    if (id == 0)
        setError(error, m_events.lastError());
    return id;
}

std::vector<Event> FeedbackService::eventsOfDay(const QString &date) const
{
    return m_events.getByDate(date);
}

std::vector<Event> FeedbackService::eventsOfRange(const QString &from, const QString &to) const
{
    return m_events.getRange(from, to);
}

QHash<QString, int> FeedbackService::eventCountsByType(const QString &date) const
{
    QHash<QString, int> counts;
    for (const Event &e : m_events.getByDate(date))
        ++counts[e.type];
    return counts;
}

bool FeedbackService::correctEvent(qint64 id, const Event &fix, const QString &reason,
                                   QString *error)
{
    if (error)
        error->clear();
    if (!m_events.correct(id, fix, reason)) {
        setError(error, m_events.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
