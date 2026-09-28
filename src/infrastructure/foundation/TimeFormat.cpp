#include "infrastructure/foundation/TimeFormat.h"

#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace PersonOS::Infrastructure {

std::string formatUtcIso(const Domain::TimePoint &tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string formatUtcDate(const Domain::TimePoint &tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d");
    return out.str();
}

} // namespace PersonOS::Infrastructure
