#pragma once

#include <optional>

#include <QString>

#include "models/Review.h"

namespace PersonOS {

// 复盘仓库（README 3.3.4）。(review_type, period_start) 唯一，upsert 语义。
// MVP 只使用日复盘（review_type='daily'）。
class ReviewRepository
{
public:
    std::optional<Review> getDaily(const QString &date) const; // YYYY-MM-DD
    bool upsert(const Review &r);   // 存在则更新，不存在则插入

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
