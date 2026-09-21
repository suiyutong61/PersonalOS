#pragma once

#include <vector>

#include <QString>

#include "models/ChangeLog.h"

namespace PersonOS {

// 变更记录仓库（README 3.3.4 / 2.4.14 原则2）
// append-only：唯一写入方式是 append()。任何重要修改都必须能回答
// "为什么改 / 改了什么 / 依据是什么"。
class ChangeLogRepository
{
public:
    qint64 append(const ChangeLog &c);               // 唯一写入方式；返回新 id，失败 0
    std::vector<ChangeLog> getByTarget(const QString &targetType,
                                       qint64 targetId) const; // ORDER BY created_at, id

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
