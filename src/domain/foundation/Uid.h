#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace PersonOS::Domain {

class Uid final
{
public:
    Uid() = default;   // 空 Uid 表示"未设置"；isValid()/empty() 检查
    static std::optional<Uid> parse(std::string_view value);

    const std::string &value() const noexcept { return m_value; }
    bool empty() const noexcept { return m_value.empty(); }

    auto operator<=>(const Uid &) const = default;

private:
    explicit Uid(std::string value) : m_value(std::move(value)) {}
    std::string m_value;
};

} // namespace PersonOS::Domain
