#pragma once

#include <string>

namespace PersonOS::Application {

enum class ErrorCode {
    Validation,
    NotFound,
    Conflict,
    Permission,
    KnowledgeInsufficient,
    ExternalUnavailable,
    Storage,
    Cancelled
};

struct ApplicationError {
    ErrorCode code = ErrorCode::Storage;
    std::string message;
    std::string detail;
    bool retryable = false;
};

} // namespace PersonOS::Application
