#pragma once

#include "datagine/core/types.hpp"

#include <string_view>

namespace datagine {

struct MarketEvent {
    EventType type{EventType::Add};
    Timestamp timestamp{};
    OrderId order_id{};
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
};

[[nodiscard]] std::string_view to_string(Side side) noexcept;
[[nodiscard]] std::string_view to_string(EventType type) noexcept;

}  // namespace datagine
