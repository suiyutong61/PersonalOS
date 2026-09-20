#pragma once

#include <optional>

#include <QString>

#include "models/Plan.h"

namespace PersonOS {

// 计划仓库（README 3.3.4）。MVP 以日计划为主；
// (period_type, period_start) 唯一，ensureDaily 幂等。
class PlanRepository
{
public:
    std::optional<Plan> getById(qint64 id) const;
    std::optional<Plan> getDaily(const QString &date) const; // YYYY-MM-DD
    qint64 ensureDaily(const QString &date);   // 不存在则创建，返回 id；失败返回 0
    bool setStatus(qint64 id, const QString &status); // active/closed

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
