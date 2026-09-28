#pragma once

#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class QtSystemClock final : public Domain::Clock
{
public:
    Domain::TimePoint now() const override;
    std::string utcIso() const override;
    std::string utcIsoPlusMinutes(int minutes) const override;
};

} // namespace PersonOS::Infrastructure
