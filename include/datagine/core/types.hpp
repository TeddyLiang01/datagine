#pragma once

#include <compare>
#include <cstdint>

namespace datagine {

enum class Side : std::uint8_t {
    Buy,
    Sell,
};

enum class EventType : std::uint8_t {
    Add,
    Modify,
    Cancel,
    Execute,
    Trade = Execute,
};

struct Price {
    using rep = std::int64_t;

    rep ticks{};

    constexpr Price() noexcept = default;
    explicit constexpr Price(rep value) noexcept : ticks(value) {}

    constexpr auto operator<=>(const Price&) const noexcept = default;
};

struct Quantity {
    using rep = std::uint64_t;

    rep units{};

    constexpr Quantity() noexcept = default;
    explicit constexpr Quantity(rep value) noexcept : units(value) {}

    constexpr auto operator<=>(const Quantity&) const noexcept = default;
};

struct OrderId {
    using rep = std::uint64_t;

    rep value{};

    constexpr OrderId() noexcept = default;
    explicit constexpr OrderId(rep id) noexcept : value(id) {}

    constexpr auto operator<=>(const OrderId&) const noexcept = default;
};

struct Timestamp {
    using rep = std::int64_t;

    rep nanoseconds{};

    constexpr Timestamp() noexcept = default;
    explicit constexpr Timestamp(rep value) noexcept : nanoseconds(value) {}

    constexpr auto operator<=>(const Timestamp&) const noexcept = default;
};

}  // namespace datagine
