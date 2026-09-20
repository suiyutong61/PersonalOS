#pragma once

#include <optional>

#include <QString>

#include "models/StateSnapshot.h"

namespace PersonOS {

// 状态快照仓库（README 3.3.4）。每日一条（date UNIQUE），upsert 语义。
class StateRepository
{
public:
    std::optional<StateSnapshot> getByDate(const QString &date) const; // YYYY-MM-DD
    bool upsert(const StateSnapshot &s);   // 存在则更新，不存在则插入

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
