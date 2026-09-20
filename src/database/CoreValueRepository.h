#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/CoreValue.h"

namespace PersonOS {

// 核心价值仓库（README 3.3.4）。修改权限：仅用户主动修改（2.6.4）。
class CoreValueRepository
{
public:
    std::vector<CoreValue> getAll() const;           // ORDER BY sort_order, id
    std::optional<CoreValue> getById(qint64 id) const;
    qint64 create(const CoreValue &v);               // 返回新 id；失败返回 0（含重名）
    bool update(const CoreValue &v);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
