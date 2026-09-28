#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Metric.h"

namespace PersonOS {

// 指标仓库（design.md 1.9.2）。(date, key, window) 唯一，upsert 语义。
// 指标由 MetricsService 按规则计算后写入；历史窗口值应保持可追溯（同日期同键
// 同窗口只更新，即"当日最新计算值"，追溯由 calibration_records 承担）。
class MetricRepository
{
public:
    std::optional<Metric> get(const QString &date, const QString &key, const QString &window) const;
    std::vector<Metric> getByDate(const QString &date) const;   // ORDER BY key
    bool upsert(const Metric &m);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
