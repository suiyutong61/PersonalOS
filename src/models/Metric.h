#pragma once

#include <QString>

// 量化指标（design.md 1.3.1-26 / FR-D-02；v0.1 设计有、MVP 未建表 → v1.0 建表）
// 对应表 metrics（每日每键每窗口一条）。指标是 Feasibility / Guardrail / 校准的
// 共同输入（NFR-18 量化可解释的数据基础）；计算规则见 MetricsService。
namespace PersonOS {

struct Metric
{
    qint64 id = 0;
    QString date;                       // YYYY-MM-DD
    QString key;                        // execution_rate / monitoring_frequency / deviation_baseline / rumination
    double value = 0.0;
    QString window;                     // 计算窗口，如 1d / 7d / 14d
    QString note;                       // 计算说明（如"无计划任务"）
    QString createdAt;
};

} // namespace PersonOS
