#pragma once

#include <chrono>

#include <string>

namespace PersonOS::Domain {

using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

class Clock
{
public:
    virtual ~Clock() = default;
    virtual TimePoint now() const = 0;
    // UTC ISO-8601 文本（数据库设计 §1.3）；供应用层记录确认时间等历史事实
    virtual std::string utcIso() const = 0;
    // 当前时刻 + N 分钟（状态 TTL 等时效计算）
    virtual std::string utcIsoPlusMinutes(int minutes) const = 0;
};

} // namespace PersonOS::Domain
