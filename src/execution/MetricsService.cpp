#include "execution/MetricsService.h"

#include <QDate>

namespace PersonOS {

namespace {

bool validDate(const QString &date)
{
    return QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid();
}

} // namespace

Metric MetricsService::executionRate(const QString &date, QString *error)
{
    if (error)
        error->clear();
    Metric m;
    m.date = date;
    m.key = QStringLiteral("execution_rate");
    m.window = QStringLiteral("1d");

    if (!validDate(date)) {
        if (error)
            *error = QStringLiteral("日期格式非法（YYYY-MM-DD）");
        return m;
    }

    qint64 planned = 0;
    qint64 actual = 0;
    for (const Task &t : m_tasks.getByDate(date)) {
        if (!t.plannedMinutes || *t.plannedMinutes <= 0)
            continue;
        planned += *t.plannedMinutes;
        if (t.actualMinutes && *t.actualMinutes > 0)
            actual += *t.actualMinutes;
    }
    if (planned == 0) {
        m.value = 0.0;
        m.note = QStringLiteral("无计划任务");
    } else {
        m.value = static_cast<double>(actual) / static_cast<double>(planned);
    }
    return m;
}

Metric MetricsService::monitoringFrequency(const QString &endDate, int windowDays, QString *error)
{
    if (error)
        error->clear();
    Metric m;
    m.date = endDate;
    m.key = QStringLiteral("monitoring_frequency");
    m.window = QStringLiteral("%1d").arg(windowDays);

    const QDate end = QDate::fromString(endDate, QStringLiteral("yyyy-MM-dd"));
    if (!end.isValid() || windowDays <= 0) {
        if (error)
            *error = QStringLiteral("日期或窗口非法");
        return m;
    }

    int recorded = 0;
    for (int i = windowDays - 1; i >= 0; --i) {
        const QString d = end.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        const bool hasEvent = !m_events.getByDate(d).empty();
        const bool hasState = m_states.getByDate(d).has_value();
        if (hasEvent || hasState)
            ++recorded;
    }
    m.value = static_cast<double>(recorded) / windowDays;
    m.note = QStringLiteral("有记录 %1/%2 天").arg(recorded).arg(windowDays);
    return m;
}

Metric MetricsService::deviationBaseline(const QString &endDate, int windowDays, QString *error)
{
    if (error)
        error->clear();
    Metric m;
    m.date = endDate;
    m.key = QStringLiteral("deviation_baseline");
    m.window = QStringLiteral("%1d").arg(windowDays);

    const QDate end = QDate::fromString(endDate, QStringLiteral("yyyy-MM-dd"));
    if (!end.isValid() || windowDays <= 0) {
        if (error)
            *error = QStringLiteral("日期或窗口非法");
        return m;
    }

    double sum = 0.0;
    int days = 0;
    for (int i = windowDays - 1; i >= 0; --i) {
        const QString d = end.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        const Metric e = executionRate(d, error);
        if (!error->isEmpty())
            return m;
        if (e.note == QStringLiteral("无计划任务"))
            continue; // 无计划任务的天不参与基线
        sum += e.value;
        ++days;
    }
    if (days == 0) {
        m.value = 0.0;
        m.note = QStringLiteral("样本不足（窗口内无计划任务）");
    } else {
        m.value = sum / days;
    }
    return m;
}

std::optional<Metric> MetricsService::rumination(const QString &date, QString *error)
{
    if (error)
        error->clear();

    const Metric e = executionRate(date, error);
    if (!error->isEmpty() || e.note == QStringLiteral("无计划任务"))
        return std::nullopt; // E 无法计算，不判定

    Metric m;
    m.date = date;
    m.key = QStringLiteral("rumination");
    m.window = QStringLiteral("1d");

    if (e.value >= 0.5) {
        m.value = 0.0;
        m.note = QStringLiteral("E≥0.5，无低执行前提");
        return m;
    }

    const auto review = m_reviews.getDaily(date);
    if (!review)
        return std::nullopt; // 当日无复盘，不判定

    const auto history = m_reviews.getBefore(date);
    if (history.size() < 3)
        return std::nullopt; // 个人复盘基线样本不足（<3 条）

    double baseline = 0.0;
    for (const Review &r : history)
        baseline += (r.summary + r.problems + r.causes + r.nextActions).size();
    baseline /= static_cast<double>(history.size());

    const int todayLen =
        (review->summary + review->problems + review->causes + review->nextActions).size();
    const bool flag = static_cast<double>(todayLen) > baseline * 1.5;
    m.value = flag ? 1.0 : 0.0;
    m.note = flag ? QStringLiteral("低执行 + 复盘文本超基线（基线 %1 字）").arg(int(baseline))
                  : QStringLiteral("低执行但复盘文本未超基线");
    return m;
}

bool MetricsService::computeDaily(const QString &date, QString *error)
{
    if (error)
        error->clear();

    if (!validDate(date)) {
        if (error)
            *error = QStringLiteral("日期格式非法（YYYY-MM-DD）");
        return false;
    }

    if (!m_repo.upsert(executionRate(date, error)))
        return false;
    if (!m_repo.upsert(monitoringFrequency(date, 7, error)))
        return false;
    if (!m_repo.upsert(deviationBaseline(date, 14, error)))
        return false;
    const auto rum = rumination(date, error);
    if (rum && !m_repo.upsert(*rum))
        return false;
    return true;
}

} // namespace PersonOS
