#pragma once

#include <optional>
#include <utility>
#include <variant>

namespace PersonOS::Application {

template<typename T, typename E>
class Result final
{
public:
    static Result success(T value) { return Result(std::move(value)); }
    static Result failure(E error) { return Result(std::move(error)); }

    bool hasValue() const noexcept { return std::holds_alternative<T>(m_data); }
    explicit operator bool() const noexcept { return hasValue(); }
    const T &value() const { return std::get<T>(m_data); }
    T &value() { return std::get<T>(m_data); }
    const E &error() const { return std::get<E>(m_data); }

private:
    explicit Result(T value) : m_data(std::move(value)) {}
    explicit Result(E error) : m_data(std::move(error)) {}
    std::variant<T, E> m_data;
};

template<typename E>
class Result<void, E> final
{
public:
    static Result success() { return Result(); }
    static Result failure(E error) { return Result(std::move(error)); }
    bool hasValue() const noexcept { return !m_error.has_value(); }
    explicit operator bool() const noexcept { return hasValue(); }
    const E &error() const { return m_error.value(); }

private:
    Result() = default;
    explicit Result(E error) : m_error(std::move(error)) {}
    std::optional<E> m_error;
};

} // namespace PersonOS::Application
