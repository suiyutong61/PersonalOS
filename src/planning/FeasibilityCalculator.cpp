#include "planning/FeasibilityCalculator.h"

#include <QDate>

#include "models/Task.h"

namespace PersonOS {

FeasibilityCalculator::Result FeasibilityCalculator::check(const QString &date, QString *error)
{
    if (error)
        error->clear();

    Result r;
    if (!QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid()) {
        if (error)
            *error = QStringLiteral("日期格式非法（YYYY-MM-DD）");
        return r;
    }

    // L_planned：当日任务的计划分钟数之和
    for (const Task &t : m_tasks.getByDate(date)) {
        if (t.plannedMinutes && *t.plannedMinutes > 0)
            r.plannedMinutes += *t.plannedMinutes;
    }

    // C_available：time_capacity（小时）× 精力节律系数（未校准 = 1.0）
    const auto capacity = m_params.get(QStringLiteral("time_capacity"));
    if (!capacity || !capacity->value || *capacity->value <= 0) {
        r.explanation = QStringLiteral("个人参数 time_capacity 缺失，无法计算可行性");
        if (error)
            *error = r.explanation;
        return r;
    }
    double energyFactor = 1.0;
    if (const auto rhythm = m_params.get(QStringLiteral("energy_rhythm")); rhythm
        && rhythm->valueText != QStringLiteral("default"))
        energyFactor = 1.0; // 精力节律校准值在 Step 6 引入；未校准恒为 1.0

    r.computable = true;
    r.capacityMinutes = static_cast<qint64>(*capacity->value * energyFactor * 60.0);

    // 阈值：workload_threshold 参数；缺失时用占位默认 1.2 并显式标注
    r.threshold = 1.2;
    bool thresholdFromParam = false;
    if (const auto th = m_params.get(QStringLiteral("workload_threshold")); th && th->value) {
        r.threshold = *th->value;
        thresholdFromParam = true;
    }

    if (r.capacityMinutes > 0)
        r.ratio = static_cast<double>(r.plannedMinutes) / static_cast<double>(r.capacityMinutes);
    r.overload = r.ratio > r.threshold;

    r.explanation = QStringLiteral(
                        "计划 %1 分钟 / 容量 %2 分钟（R=%3，阈值 %4[%5]）→ %6")
                        .arg(r.plannedMinutes)
                        .arg(r.capacityMinutes)
                        .arg(QString::number(r.ratio, 'f', 2))
                        .arg(QString::number(r.threshold, 'f', 2))
                        .arg(thresholdFromParam
                                 ? QStringLiteral("参数值")
                                 : QStringLiteral("占位默认，待研究证据校准"))
                        .arg(r.overload ? QStringLiteral("超载") : QStringLiteral("可行"));
    return r;
}

} // namespace PersonOS
