#pragma once

#include <optional>

#include <QString>

#include "database/EventRepository.h"
#include "database/MetricRepository.h"
#include "database/ReviewRepository.h"
#include "database/StateRepository.h"
#include "database/TaskRepository.h"
#include "models/Metric.h"

namespace PersonOS {

// 量化指标计算（design.md 1.4.1 Execution 扩展 / 1.10.2；FR-D-02）
// 指标是 Feasibility / Guardrail / 校准的共同输入（NFR-18 量化可解释的数据基础）。
// 计算规则见 design.md 1.10.2；"禁止归因懒惰"等纪律在 Guardrail（Step 3）落地。
class MetricsService
{
public:
    // E = Σ actual / Σ planned（当日有计划的任务）；无计划任务 → value=0 附 note
    Metric executionRate(const QString &date, QString *error = nullptr);

    // 有记录天数 / 窗口天数；"有记录" = 当日存在 Event 或 StateSnapshot（[证据 P12] 中介变量）
    Metric monitoringFrequency(const QString &endDate, int windowDays, QString *error = nullptr);

    // 窗口内每日执行率的均值（只统计有计划任务的天）；样本不足 → value=0 附 note
    Metric deviationBaseline(const QString &endDate, int windowDays, QString *error = nullptr);

    // 反刍信号（[证据 P2] 代理指标，design.md 1.10.2）：
    // E_day < 0.5 且当日复盘文本长度 > 个人复盘基线 × 1.5 → value=1.0。
    // 返回 nullopt = 无法判定（无计划任务 / 当日无复盘 / 历史复盘不足 3 条），不武断判定。
    std::optional<Metric> rumination(const QString &date, QString *error = nullptr);

    // 一次计算当日全部指标并落库（execution_rate 1d / monitoring_frequency 7d /
    // deviation_baseline 14d / rumination 1d）；幂等（同键同窗口 upsert）
    bool computeDaily(const QString &date, QString *error = nullptr);

    QString lastError() const { return m_repo.lastError(); }

private:
    MetricRepository m_repo;
    TaskRepository m_tasks;
    EventRepository m_events;
    StateRepository m_states;
    ReviewRepository m_reviews;
};

} // namespace PersonOS
