#include "feedback/GuardrailService.h"

#include <QDate>

#include "models/Task.h"

namespace PersonOS {

namespace {

bool validDate(const QString &date)
{
    return QDate::fromString(date, QStringLiteral("yyyy-MM-dd")).isValid();
}

} // namespace

std::optional<GuardrailWarning> GuardrailService::checkWorkload(const QString &date, QString *error)
{
    if (error)
        error->clear();

    if (!validDate(date)) {
        if (error)
            *error = QStringLiteral("日期格式非法（YYYY-MM-DD）");
        return std::nullopt;
    }

    // 按日幂等：已有当日 active workload 预警则直接返回，不重复生成
    if (const auto existing = m_repo.activeForDate(QStringLiteral("workload"), date))
        return existing;

    const FeasibilityCalculator::Result r = m_feasibility.check(date, error);
    if (!error->isEmpty())
        return std::nullopt;
    if (!r.overload)
        return std::nullopt; // 可行，无需预警

    GuardrailWarning w;
    w.date = date;
    w.type = QStringLiteral("workload");
    w.rule = QStringLiteral("R = L_planned / C_available > workload_threshold");
    w.dataSnapshot = QStringLiteral("planned=%1min, capacity=%2min, R=%3")
                         .arg(r.plannedMinutes)
                         .arg(r.capacityMinutes)
                         .arg(QString::number(r.ratio, 'f', 2));
    w.threshold = QStringLiteral("%1（%2）")
                      .arg(QString::number(r.threshold, 'f', 2))
                      .arg(QStringLiteral("population_default 占位，待研究证据校准"));
    w.suggestion = QStringLiteral("压缩 / 延期 / 拆分 / 调整优先级 / 降低范围——"
                                  "AI 只建议，最终决定权在用户");
    w.evidenceRef = QStringLiteral("待研究证据（领域② Plan Feasibility）"); // NFR-13 显式标注

    const qint64 id = m_repo.append(w);
    if (id == 0) {
        if (error)
            *error = m_repo.lastError();
        return std::nullopt;
    }
    w.id = id;
    return w;
}

QList<GuardrailWarning> GuardrailService::active(const QString &date) const
{
    const auto v = m_repo.active(date);
    return QList<GuardrailWarning>(v.begin(), v.end());
}

std::optional<GuardrailWarning> GuardrailService::checkStall(const QString &endDate, QString *error)
{
    if (error)
        error->clear();

    if (!validDate(endDate)) {
        if (error)
            *error = QStringLiteral("日期格式非法（YYYY-MM-DD）");
        return std::nullopt;
    }

    // 按日幂等：已有当日 active stall 预警则直接返回
    if (const auto existing = m_repo.activeForDate(QStringLiteral("stall"), endDate))
        return existing;

    // 触发参数（population_default 占位，待研究证据校准——同 workload_threshold 策略）
    double stallThreshold = 0.4;
    if (const auto th = m_params.get(QStringLiteral("stall_threshold")); th && th->value)
        stallThreshold = *th->value;
    int quietDays = 3;
    if (const auto qd = m_params.get(QStringLiteral("stall_quiet_days")); qd && qd->value)
        quietDays = static_cast<int>(*qd->value);

    const QDate end = QDate::fromString(endDate, QStringLiteral("yyyy-MM-dd"));
    constexpr int kWindowDays = 7;

    // E_7d：窗口化执行率（Σ actual / Σ planned，只统计有计划任务的天）
    qint64 planned = 0;
    qint64 actual = 0;
    int sampleDays = 0;
    for (int i = kWindowDays - 1; i >= 0; --i) {
        const QString d = end.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        qint64 pDay = 0;
        qint64 aDay = 0;
        for (const Task &t : m_tasks.getByDate(d)) {
            if (t.plannedMinutes && *t.plannedMinutes > 0) {
                pDay += *t.plannedMinutes;
                if (t.actualMinutes && *t.actualMinutes > 0)
                    aDay += *t.actualMinutes;
            }
        }
        if (pDay > 0) {
            planned += pDay;
            actual += aDay;
            ++sampleDays;
        }
    }
    if (sampleDays == 0)
        return std::nullopt; // 窗口内无计划任务，不判定（无数据 ≠ 停滞）

    const double e7 = planned > 0 ? static_cast<double>(actual) / static_cast<double>(planned)
                                  : 0.0;

    // 连续无完成 streak：有任务但无 completed 的天数（无任务的天跳过不计）
    int streak = 0;
    for (int i = 0; i < 60; ++i) {
        const QString d = end.addDays(-i).toString(QStringLiteral("yyyy-MM-dd"));
        bool hasTasks = false;
        bool hasCompleted = false;
        for (const Task &t : m_tasks.getByDate(d)) {
            if (t.plannedMinutes && *t.plannedMinutes > 0) {
                hasTasks = true;
                if (t.status == QStringLiteral("completed"))
                    hasCompleted = true;
            }
        }
        if (!hasTasks)
            continue;
        if (hasCompleted)
            break;
        ++streak;
    }

    const bool rule1 = e7 < stallThreshold && sampleDays >= 3;
    const bool rule2 = streak >= quietDays;
    if (!rule1 && !rule2)
        return std::nullopt;

    // 反刍信号（作诊断参考，[证据 P2] 代理）
    double rum = -1.0;
    if (const auto rm = m_metricRepo.get(endDate, QStringLiteral("rumination"),
                                         QStringLiteral("1d")))
        rum = rm->value;

    QString suggestion =
        QStringLiteral(
            "Recovery 建议：减少非核心任务 / 保护重要任务 / 降低短期计划负荷 / 恢复基本作息。\n"
            "多因素诊断清单：工作量 / 时间容量 / 睡眠恢复 / 压力 / 任务难度 / 拆解 / "
            "环境摩擦 / 目标冲突 / 动机 / 优先级变化。\n"
            "系统纪律：禁止归因\"懒惰\"——诊断草案由用户确认。");

    // Diagnostic 升级（design.md 1.5.2）：21 天内 stall 预警 ≥3 次 = 异常持续
    const QString from = end.addDays(-20).toString(QStringLiteral("yyyy-MM-dd"));
    if (m_repo.countSince(QStringLiteral("stall"), from, endDate) >= 3)
        suggestion += QStringLiteral(
            "\nDiagnostic 阶段：异常持续，建议收集相关数据 → 建立假设 → 设计验证方案"
            "（衔接 N-of-1 实验，Step 7）。");

    GuardrailWarning w;
    w.date = endDate;
    w.type = QStringLiteral("stall");
    w.rule = rule1
                 ? QStringLiteral("E_7d < stall_threshold 且样本天数 ≥ 3")
                 : QStringLiteral("连续 %1 天有计划任务但无完成").arg(quietDays);
    w.dataSnapshot = QStringLiteral("E_7d=%1, 样本天数=%2, 连续无完成=%3天, rumination=%4")
                         .arg(QString::number(e7, 'f', 2))
                         .arg(sampleDays)
                         .arg(streak)
                         .arg(rum >= 0 ? QString::number(rum) : QStringLiteral("无"));
    w.threshold = QStringLiteral("stall_threshold=%1 / stall_quiet_days=%2"
                                 "（population_default 占位，待研究证据校准）")
                      .arg(QString::number(stallThreshold, 'f', 2))
                      .arg(quietDays);
    w.suggestion = suggestion;
    w.evidenceRef = QStringLiteral("待研究证据（领域② Plan Feasibility / ③ Time Management / ⑬ Procrastination）");

    const qint64 id = m_repo.append(w);
    if (id == 0) {
        if (error)
            *error = m_repo.lastError();
        return std::nullopt;
    }
    w.id = id;
    return w;
}

bool GuardrailService::dismiss(qint64 warningId, const QString &reason, QString *error)
{
    if (error)
        error->clear();
    if (!m_repo.setStatus(warningId, QStringLiteral("dismissed"), reason)) {
        if (error) {
            // 无 SQL 错误但无匹配行 = 预警不存在（error 只描述本次调用的结果）
            *error = m_repo.lastError().isEmpty()
                         ? QStringLiteral("预警不存在（id=%1）").arg(warningId)
                         : m_repo.lastError();
        }
        return false;
    }
    return true;
}

} // namespace PersonOS
