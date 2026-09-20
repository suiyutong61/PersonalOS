#pragma once

#include <vector>

#include <QString>

#include "models/Event.h"

namespace PersonOS {

// 事件仓库（README 3.3.4）。append-only：唯一写入方式是 append()，
// 不提供 UPDATE/DELETE；修正走留痕流程（2.4.14 原则1），correct() 在 Step 9 实现。
class EventRepository
{
public:
    qint64 append(const Event &e);                   // 唯一写入方式；返回新 id，失败 0
    std::vector<Event> getByDate(const QString &date) const;  // ORDER BY occurred_at, id
    std::vector<Event> getRange(const QString &from, const QString &to) const; // 含端点

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
