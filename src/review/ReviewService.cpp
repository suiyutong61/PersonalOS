#include "review/ReviewService.h"

#include <QDate>

#include "models/Task.h"

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool validDate(const QString &date)
{
    return QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid();
}

} // namespace

std::optional<Review> ReviewService::dailyReview(const QString &date) const
{
    return m_reviews.getDaily(date);
}

Review ReviewService::draftDailyReview(const QString &date) const
{
    Review draft;
    draft.reviewType = QStringLiteral("daily");
    draft.periodStart = date;

    // ---- 计划 vs 实际（依据任务状态，2.5.6）----
    const auto tasks = m_tasks.getByDate(date);
    int completed = 0, skipped = 0, cancelled = 0;
    int plannedMinutes = 0, actualMinutes = 0;
    for (const Task &t : tasks) {
        if (t.plannedMinutes)
            plannedMinutes += *t.plannedMinutes;
        if (t.status == QStringLiteral("completed")) {
            ++completed;
            if (t.actualMinutes)
                actualMinutes += *t.actualMinutes;
        } else if (t.status == QStringLiteral("skipped")) {
            ++skipped;
        } else if (t.status == QStringLiteral("cancelled")) {
            ++cancelled;
        }
    }
    const int total = static_cast<int>(tasks.size());
    const int pending = total - completed - skipped - cancelled;

    QStringList lines;
    if (total == 0) {
        lines << QStringLiteral("当日无计划任务。");
    } else {
        lines << QStringLiteral("计划任务 %1 项，计划时长 %2 分钟").arg(total).arg(plannedMinutes);
        lines << QStringLiteral("完成 %1 项（实际 %2 分钟），跳过 %3 项，取消 %4 项，待处理 %5 项")
                     .arg(completed)
                     .arg(actualMinutes)
                     .arg(skipped)
                     .arg(cancelled)
                     .arg(pending);
        lines << QStringLiteral("任务完成率: %1%").arg(completed * 100 / total);
        lines << QStringLiteral("时长偏差: %1 分钟（计划 %2 / 实际 %3）")
                     .arg(actualMinutes - plannedMinutes)
                     .arg(plannedMinutes)
                     .arg(actualMinutes);
    }

    // ---- 当日状态摘要（事实层，2.4.3.4）----
    if (const auto state = m_states.getByDate(date)) {
        QStringList parts;
        if (state->sleepHours)
            parts << QStringLiteral("睡眠 %1h").arg(*state->sleepHours);
        if (state->energy)
            parts << QStringLiteral("精力 %1").arg(*state->energy);
        if (state->focus)
            parts << QStringLiteral("专注 %1").arg(*state->focus);
        if (state->mood)
            parts << QStringLiteral("心情 %1").arg(*state->mood);
        if (!parts.isEmpty())
            lines << QStringLiteral("当日状态: ") + parts.join(QStringLiteral(" / "));
    }

    draft.summary = lines.join(u'\n');
    return draft;
}

bool ReviewService::saveDailyReview(const Review &r, QString *error)
{
    if (error)
        error->clear();
    if (r.reviewType != QStringLiteral("daily")) {
        setError(error, QStringLiteral("MVP 仅支持日复盘（review_type=daily）"));
        return false;
    }
    if (!validDate(r.periodStart)) {
        setError(error, QStringLiteral("复盘日期无效: %1（应为 YYYY-MM-DD）").arg(r.periodStart));
        return false;
    }
    if (r.summary.trimmed().isEmpty()) {
        setError(error, QStringLiteral("复盘总结不能为空"));
        return false;
    }
    if (!m_reviews.upsert(r)) {
        setError(error, m_reviews.lastError());
        return false;
    }
    return true;
}

QStringList ReviewService::pendingReviewDates(int maxDaysBack) const
{
    QStringList out;
    const QDate today = QDate::currentDate();
    for (int i = 1; i <= maxDaysBack; ++i) {
        const QString date = today.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        if (m_reviews.getDaily(date))
            continue;          // 已复盘
        if (m_plans.getDaily(date))
            out << date;       // 有计划但未复盘 → 待补录
    }
    return out;
}

} // namespace PersonOS
