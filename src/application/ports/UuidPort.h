#pragma once

#include "domain/foundation/Uid.h"

// UID 生成端口（DD-001 §5.4）
namespace PersonOS::Application {

class UuidPort
{
public:
    virtual ~UuidPort() = default;
    virtual Domain::Uid next() = 0;
};

} // namespace PersonOS::Application
