#include "domain/foundation/Uid.h"

#include <cctype>

namespace PersonOS::Domain {

std::optional<Uid> Uid::parse(std::string_view value)
{
    // RFC 4122 textual shape. Version/variant are deliberately not restricted so
    // imported UUID-compatible identifiers can be preserved.
    if (value.size() != 36)
        return std::nullopt;

    for (std::size_t i = 0; i < value.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        if (separator) {
            if (value[i] != '-')
                return std::nullopt;
        } else if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return std::nullopt;
        }
    }
    return Uid(std::string(value));
}

} // namespace PersonOS::Domain
