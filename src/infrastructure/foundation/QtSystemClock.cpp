#include "infrastructure/foundation/QtSystemClock.h"

#include <chrono>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

Domain::TimePoint QtSystemClock::now() const
{
    return std::chrono::system_clock::now();
}

std::string QtSystemClock::utcIso() const
{
    return formatUtcIso(now());
}

std::string QtSystemClock::utcIsoPlusMinutes(int minutes) const
{
    return formatUtcIso(now() + std::chrono::minutes(minutes));
}

} // namespace PersonOS::Infrastructure
