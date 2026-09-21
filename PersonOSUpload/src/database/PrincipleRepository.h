#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Principle.h"

namespace PersonOS {

// 原则仓库（README 3.3.4）。修改权限：用户确认后修改（2.6.5）。
class PrincipleRepository
{
public:
    std::vector<Principle> getAll() const;           // ORDER BY sort_order, id
    std::optional<Principle> getById(qint64 id) const;
    qint64 create(const Principle &p);               // 返回新 id；失败返回 0
    bool update(const Principle &p);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
