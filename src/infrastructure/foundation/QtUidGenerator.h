#pragma once

#include "application/ports/UuidPort.h"
#include "domain/foundation/Uid.h"

namespace PersonOS::Infrastructure {

class QtUidGenerator final : public Application::UuidPort
{
public:
    Domain::Uid next() override;
};

} // namespace PersonOS::Infrastructure
