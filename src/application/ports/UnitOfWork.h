#pragma once

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

namespace PersonOS::Application {

class UnitOfWork
{
public:
    virtual ~UnitOfWork() = default;
    virtual Result<void, ApplicationError> begin() = 0;
    virtual Result<void, ApplicationError> commit() = 0;
    virtual void rollback() noexcept = 0;
    virtual bool active() const noexcept = 0;
};

} // namespace PersonOS::Application
