#pragma once

#include <vector>

#include <QString>

#include "models/CalibrationRecord.h"

namespace PersonOS {

// 校准记录仓库（design.md 1.9.2）。append-only：唯一写入方式是 append()，
// 不提供 UPDATE/DELETE（NFR-17 校准留痕纪律）。
class CalibrationRepository
{
public:
    qint64 append(const CalibrationRecord &r);          // 返回新 id；失败返回 0
    std::vector<CalibrationRecord> getByParam(const QString &key) const; // ORDER BY id

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
