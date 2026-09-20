#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Event.h"

namespace PersonOS {

// 事件仓库（README 3.3.4）。append-only：唯一写入方式是 append()，
// 不提供 UPDATE/DELETE；修正走留痕流程（2.4.14 原则1）：
// correct() = 先写 change_logs（原始 → 修正 + 原因），再更新事件内容。
class EventRepository
{
public:
    qint64 append(const Event &e);                   // 唯一写入方式；返回新 id，失败 0
    std::optional<Event> getById(qint64 id) const;
    std::vector<Event> getByDate(const QString &date) const;  // ORDER BY occurred_at, id
    std::vector<Event> getRange(const QString &from, const QString &to) const; // 含端点

    // 修正事件内容（仅 type/title/description），必须提供原因；先留痕再更新
    bool correct(qint64 id, const Event &fix, const QString &reason);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
