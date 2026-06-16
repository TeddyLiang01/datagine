#include "datagine/book/limit_order_book.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace datagine {

std::string_view to_string(BookResult result) noexcept {
    switch (result) {
        case BookResult::Applied:
            return "applied";
        case BookResult::DuplicateOrderId:
            return "duplicate order id";
        case BookResult::MissingOrderId:
            return "missing order id";
        case BookResult::InvalidQuantity:
            return "invalid quantity";
        case BookResult::InsufficientQuantity:
            return "insufficient quantity";
        case BookResult::UnsupportedModify:
            return "unsupported modify";
        case BookResult::CapacityExceeded:
            return "capacity exceeded";
    }

    return "unknown";
}

BookResult LimitOrderBook::add_order(
    OrderId order_id,
    Side side,
    Price price,
    Quantity quantity,
    Timestamp timestamp) {
    if (quantity.units == 0) {
        return BookResult::InvalidQuantity;
    }

    if (order_lookup_.contains(order_id.value)) {
        return BookResult::DuplicateOrderId;
    }

    order_lookup_.reserve(order_lookup_.size() + 1);

    PriceLevel* level = nullptr;
    if (side == Side::Buy) {
        level = &bid_levels_[price.ticks];
    } else {
        level = &ask_levels_[price.ticks];
    }

    level->orders.push_back(OrderEntry{order_id, timestamp, quantity});
    auto order_it = std::prev(level->orders.end());
    level->total_quantity.units += quantity.units;

    order_lookup_.emplace(
        order_id.value,
        OrderRecord{side, price, timestamp, quantity, order_it});

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult LimitOrderBook::cancel_order(OrderId order_id) {
    const auto order_it = order_lookup_.find(order_id.value);
    if (order_it == order_lookup_.end()) {
        return BookResult::MissingOrderId;
    }

    remove_existing_order(order_it);
    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult LimitOrderBook::modify_order(OrderId order_id, Quantity new_quantity) {
    if (new_quantity.units == 0) {
        return BookResult::InvalidQuantity;
    }

    const auto order_it = order_lookup_.find(order_id.value);
    if (order_it == order_lookup_.end()) {
        return BookResult::MissingOrderId;
    }

    auto& record = order_it->second;
    if (new_quantity.units > record.quantity.units) {
        return BookResult::UnsupportedModify;
    }

    if (new_quantity.units == record.quantity.units) {
        ++stats_.events_applied;
        return BookResult::Applied;
    }

    auto* level = find_level(record.side, record.price);
    const auto reduction = record.quantity.units - new_quantity.units;
    level->total_quantity.units -= reduction;
    record.quantity = new_quantity;
    record.iterator->quantity = new_quantity;

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult LimitOrderBook::execute_order(OrderId order_id, Quantity executed_quantity) {
    if (executed_quantity.units == 0) {
        return BookResult::InvalidQuantity;
    }

    const auto order_it = order_lookup_.find(order_id.value);
    if (order_it == order_lookup_.end()) {
        return BookResult::MissingOrderId;
    }

    auto& record = order_it->second;
    if (executed_quantity.units > record.quantity.units) {
        return BookResult::InsufficientQuantity;
    }

    if (executed_quantity.units == record.quantity.units) {
        remove_existing_order(order_it);
        ++stats_.events_applied;
        return BookResult::Applied;
    }

    auto* level = find_level(record.side, record.price);
    level->total_quantity.units -= executed_quantity.units;
    record.quantity.units -= executed_quantity.units;
    record.iterator->quantity = record.quantity;

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult LimitOrderBook::apply(const MarketEvent& event) {
    switch (event.type) {
        case EventType::Add:
            return add_order(
                event.order_id,
                event.side,
                event.price,
                event.quantity,
                event.timestamp);
        case EventType::Modify:
            return modify_order(event.order_id, event.quantity);
        case EventType::Cancel:
            return cancel_order(event.order_id);
        case EventType::Execute:
            return execute_order(event.order_id, event.quantity);
    }

    return BookResult::UnsupportedModify;
}

void LimitOrderBook::clear() noexcept {
    bid_levels_.clear();
    ask_levels_.clear();
    order_lookup_.clear();
    stats_ = Stats{};
}

TopOfBook LimitOrderBook::top_of_book() const {
    TopOfBook top{};

    if (!bid_levels_.empty()) {
        const auto& [price, level] = *bid_levels_.begin();
        top.bid = make_price_level_view(price, level);
    }

    if (!ask_levels_.empty()) {
        const auto& [price, level] = *ask_levels_.begin();
        top.ask = make_price_level_view(price, level);
    }

    return top;
}

BookDepth LimitOrderBook::depth(std::size_t levels) const {
    BookDepth result{};
    result.bids.reserve(std::min(levels, bid_levels_.size()));
    result.asks.reserve(std::min(levels, ask_levels_.size()));

    for (auto it = bid_levels_.begin(); it != bid_levels_.end() && result.bids.size() < levels; ++it) {
        result.bids.push_back(make_price_level_view(it->first, it->second));
    }

    for (auto it = ask_levels_.begin(); it != ask_levels_.end() && result.asks.size() < levels; ++it) {
        result.asks.push_back(make_price_level_view(it->first, it->second));
    }

    return result;
}

BookSnapshot LimitOrderBook::snapshot() const {
    BookSnapshot result{};
    result.bids.reserve(bid_levels_.size());
    result.asks.reserve(ask_levels_.size());

    for (const auto& [price, level] : bid_levels_) {
        PriceLevelSnapshot level_snapshot{
            Price{price},
            level.total_quantity,
            {},
        };
        level_snapshot.orders.reserve(level.orders.size());
        for (const auto& order : level.orders) {
            level_snapshot.orders.push_back(OrderView{
                order.order_id,
                order.timestamp,
                order.quantity,
            });
        }
        result.bids.push_back(std::move(level_snapshot));
    }

    for (const auto& [price, level] : ask_levels_) {
        PriceLevelSnapshot level_snapshot{
            Price{price},
            level.total_quantity,
            {},
        };
        level_snapshot.orders.reserve(level.orders.size());
        for (const auto& order : level.orders) {
            level_snapshot.orders.push_back(OrderView{
                order.order_id,
                order.timestamp,
                order.quantity,
            });
        }
        result.asks.push_back(std::move(level_snapshot));
    }

    return result;
}

std::vector<OrderId> LimitOrderBook::order_ids_at_price(Side side, Price price) const {
    std::vector<OrderId> result{};
    const auto* level = find_level(side, price);
    if (level == nullptr) {
        return result;
    }

    result.reserve(level->orders.size());
    for (const auto& order : level->orders) {
        result.push_back(order.order_id);
    }

    return result;
}

bool LimitOrderBook::validate_invariants(InvariantOptions options) const {
    if (!options.allow_crossed_book && !bid_levels_.empty() && !ask_levels_.empty()) {
        if (bid_levels_.begin()->first > ask_levels_.begin()->first) {
            return false;
        }
    }

    const auto top = top_of_book();
    if (bid_levels_.empty() != !top.bid.has_value()) {
        return false;
    }

    if (ask_levels_.empty() != !top.ask.has_value()) {
        return false;
    }

    if (!bid_levels_.empty()) {
        const auto& [price, level] = *bid_levels_.begin();
        if (*top.bid != make_price_level_view(price, level)) {
            return false;
        }
    }

    if (!ask_levels_.empty()) {
        const auto& [price, level] = *ask_levels_.begin();
        if (*top.ask != make_price_level_view(price, level)) {
            return false;
        }
    }

    std::unordered_set<OrderId::rep> seen_order_ids{};
    seen_order_ids.reserve(order_lookup_.size());

    const auto validate_level = [this, &seen_order_ids](Side side, Price::rep price, const PriceLevel& level) {
        if (level.orders.empty()) {
            return false;
        }

        Quantity::rep total_quantity = 0;
        for (auto it = level.orders.begin(); it != level.orders.end(); ++it) {
            const auto& order = *it;
            if (order.quantity.units == 0) {
                return false;
            }

            if (!seen_order_ids.insert(order.order_id.value).second) {
                return false;
            }

            const auto lookup_it = order_lookup_.find(order.order_id.value);
            if (lookup_it == order_lookup_.end()) {
                return false;
            }

            const auto& record = lookup_it->second;
            if (record.side != side) {
                return false;
            }

            if (record.price.ticks != price) {
                return false;
            }

            if (record.quantity != order.quantity) {
                return false;
            }

            if (record.timestamp != order.timestamp) {
                return false;
            }

            if (record.iterator != it) {
                return false;
            }

            total_quantity += order.quantity.units;
        }

        return total_quantity == level.total_quantity.units;
    };

    for (const auto& [price, level] : bid_levels_) {
        if (!validate_level(Side::Buy, price, level)) {
            return false;
        }
    }

    for (const auto& [price, level] : ask_levels_) {
        if (!validate_level(Side::Sell, price, level)) {
            return false;
        }
    }

    if (seen_order_ids.size() != order_lookup_.size()) {
        return false;
    }

    for (const auto& [order_id, record] : order_lookup_) {
        if (!seen_order_ids.contains(order_id)) {
            return false;
        }

        const auto* level = find_level(record.side, record.price);
        if (level == nullptr) {
            return false;
        }
    }

    return true;
}

const LimitOrderBook::Stats& LimitOrderBook::stats() const noexcept {
    return stats_;
}

std::uint64_t LimitOrderBook::events_applied() const noexcept {
    return stats_.events_applied;
}

std::size_t LimitOrderBook::live_order_count() const noexcept {
    return order_lookup_.size();
}

LimitOrderBook::PriceLevel* LimitOrderBook::find_level(Side side, Price price) noexcept {
    if (side == Side::Buy) {
        const auto it = bid_levels_.find(price.ticks);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }

    const auto it = ask_levels_.find(price.ticks);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

const LimitOrderBook::PriceLevel* LimitOrderBook::find_level(Side side, Price price) const noexcept {
    if (side == Side::Buy) {
        const auto it = bid_levels_.find(price.ticks);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }

    const auto it = ask_levels_.find(price.ticks);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

PriceLevelView LimitOrderBook::make_price_level_view(Price::rep price, const PriceLevel& level) noexcept {
    return PriceLevelView{
        Price{price},
        level.total_quantity,
        level.orders.size(),
    };
}

void LimitOrderBook::remove_existing_order(OrderLookup::iterator order_it) noexcept {
    const auto record = order_it->second;

    if (record.side == Side::Buy) {
        auto level_it = bid_levels_.find(record.price.ticks);
        auto& level = level_it->second;
        level.total_quantity.units -= record.quantity.units;
        level.orders.erase(record.iterator);
        if (level.orders.empty()) {
            bid_levels_.erase(level_it);
        }
    } else {
        auto level_it = ask_levels_.find(record.price.ticks);
        auto& level = level_it->second;
        level.total_quantity.units -= record.quantity.units;
        level.orders.erase(record.iterator);
        if (level.orders.empty()) {
            ask_levels_.erase(level_it);
        }
    }

    order_lookup_.erase(order_it);
}

}  // namespace datagine
