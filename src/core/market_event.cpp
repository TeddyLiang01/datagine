#include "datagine/core/market_event.hpp"

namespace datagine {

std::string_view to_string(Side side) noexcept {
    switch (side) {
        case Side::Buy:
            return "buy";
        case Side::Sell:
            return "sell";
    }

    return "unknown";
}

std::string_view to_string(EventType type) noexcept {
    switch (type) {
        case EventType::Add:
            return "add";
        case EventType::Modify:
            return "modify";
        case EventType::Cancel:
            return "cancel";
        case EventType::Execute:
            return "execute";
    }

    return "unknown";
}

}  // namespace datagine
