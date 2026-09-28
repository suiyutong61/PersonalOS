#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/GuardrailWarning.h"

namespace PersonOS {

// Guardrail 预警仓库（design.md 1.9.2）。预警按日幂等（同日同类型 active 唯一，
// 由 GuardrailService 保证）；历史预警保留（可追溯，FR-E-04）。
class GuardrailRepository
{
public:
    qint64 append(const GuardrailWarning &w);                // 返回新 id；失败返回 0
    std::optional<GuardrailWarning> activeForDate(const QString &type,
                                                  const QString &date) const;
    std::vector<GuardrailWarning> active(const QString &date) const; // 当日全部 active
    bool setStatus(qint64 id, const QString &status, const QString &handledBy); // handled→原因
    // v1.0 Step 3：日期范围内该类型预警总数（含已处置，用于 Diagnostic 升级判定）
    int countSince(const QString &type, const QString &from, const QString &to) const;

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
