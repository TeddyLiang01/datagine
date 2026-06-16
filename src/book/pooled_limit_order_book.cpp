#include "datagine/book/pooled_limit_order_book.hpp"

#include <algorithm>
#include <unordered_set>

namespace datagine {

PooledLimitOrderBook::PooledLimitOrderBook(std::size_t max_live_orders)
    : nodes_(max_live_orders) {
    free_indices_.reserve(max_live_orders);
    order_lookup_.reserve(max_live_orders);
    reset_free_list();
}

BookResult PooledLimitOrderBook::add_order(
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

    auto node_index = allocate_node();
    if (!node_index.has_value()) {
        return BookResult::CapacityExceeded;
    }

    PriceLevel* level = nullptr;
    if (side == Side::Buy) {
        level = &bid_levels_[price.ticks];
    } else {
        level = &ask_levels_[price.ticks];
    }

    auto& node = nodes_[*node_index];
    node.order_id = order_id;
    node.timestamp = timestamp;
    node.quantity = quantity;
    node.active = true;

    append_node(*level, *node_index);
    level->total_quantity.units += quantity.units;

    order_lookup_.emplace(
        order_id.value,
        OrderRecord{side, price, timestamp, quantity, *node_index});

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult PooledLimitOrderBook::cancel_order(OrderId order_id) {
    const auto order_it = order_lookup_.find(order_id.value);
    if (order_it == order_lookup_.end()) {
        return BookResult::MissingOrderId;
    }

    remove_existing_order(order_it);
    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult PooledLimitOrderBook::modify_order(OrderId order_id, Quantity new_quantity) {
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
    nodes_[record.node_index].quantity = new_quantity;

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult PooledLimitOrderBook::execute_order(OrderId order_id, Quantity executed_quantity) {
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
    nodes_[record.node_index].quantity = record.quantity;

    ++stats_.events_applied;
    return BookResult::Applied;
}

BookResult PooledLimitOrderBook::apply(const MarketEvent& event) {
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

void PooledLimitOrderBook::clear() {
    bid_levels_.clear();
    ask_levels_.clear();
    order_lookup_.clear();
    stats_ = Stats{};
    reset_free_list();
}

TopOfBook PooledLimitOrderBook::top_of_book() const {
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

BookDepth PooledLimitOrderBook::depth(std::size_t levels) const {
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

BookSnapshot PooledLimitOrderBook::snapshot() const {
    BookSnapshot result{};
    result.bids.reserve(bid_levels_.size());
    result.asks.reserve(ask_levels_.size());

    const auto append_level = [this](Price::rep price, const PriceLevel& level) {
        PriceLevelSnapshot level_snapshot{
            Price{price},
            level.total_quantity,
            {},
        };
        level_snapshot.orders.reserve(level.order_count);

        for (auto index = level.head; index != npos; index = nodes_[index].next) {
            const auto& node = nodes_[index];
            level_snapshot.orders.push_back(OrderView{
                node.order_id,
                node.timestamp,
                node.quantity,
            });
        }

        return level_snapshot;
    };

    for (const auto& [price, level] : bid_levels_) {
        result.bids.push_back(append_level(price, level));
    }

    for (const auto& [price, level] : ask_levels_) {
        result.asks.push_back(append_level(price, level));
    }

    return result;
}

std::vector<OrderId> PooledLimitOrderBook::order_ids_at_price(Side side, Price price) const {
    std::vector<OrderId> result{};
    const auto* level = find_level(side, price);
    if (level == nullptr) {
        return result;
    }

    result.reserve(level->order_count);
    for (auto index = level->head; index != npos; index = nodes_[index].next) {
        result.push_back(nodes_[index].order_id);
    }

    return result;
}

bool PooledLimitOrderBook::validate_invariants(InvariantOptions options) const {
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
        if (level.order_count == 0 || level.head == npos || level.tail == npos) {
            return false;
        }

        Quantity::rep total_quantity = 0;
        std::size_t visited_count = 0;
        auto previous = npos;

        for (auto index = level.head; index != npos; index = nodes_[index].next) {
            if (index >= nodes_.size()) {
                return false;
            }

            const auto& node = nodes_[index];
            if (!node.active) {
                return false;
            }

            if (node.prev != previous) {
                return false;
            }

            if (node.quantity.units == 0) {
                return false;
            }

            if (!seen_order_ids.insert(node.order_id.value).second) {
                return false;
            }

            const auto lookup_it = order_lookup_.find(node.order_id.value);
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

            if (record.quantity != node.quantity) {
                return false;
            }

            if (record.timestamp != node.timestamp) {
                return false;
            }

            if (record.node_index != index) {
                return false;
            }

            total_quantity += node.quantity.units;
            ++visited_count;
            previous = index;
        }

        if (previous != level.tail) {
            return false;
        }

        return visited_count == level.order_count && total_quantity == level.total_quantity.units;
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

        if (record.node_index >= nodes_.size() || !nodes_[record.node_index].active) {
            return false;
        }

        const auto* level = find_level(record.side, record.price);
        if (level == nullptr) {
            return false;
        }
    }

    return true;
}

const PooledLimitOrderBook::Stats& PooledLimitOrderBook::stats() const noexcept {
    return stats_;
}

std::uint64_t PooledLimitOrderBook::events_applied() const noexcept {
    return stats_.events_applied;
}

std::size_t PooledLimitOrderBook::live_order_count() const noexcept {
    return order_lookup_.size();
}

std::size_t PooledLimitOrderBook::capacity() const noexcept {
    return nodes_.size();
}

std::optional<std::size_t> PooledLimitOrderBook::allocate_node() noexcept {
    if (free_indices_.empty()) {
        return std::nullopt;
    }

    const auto index = free_indices_.back();
    free_indices_.pop_back();
    return index;
}

void PooledLimitOrderBook::release_node(std::size_t node_index) noexcept {
    nodes_[node_index] = OrderNode{};
    free_indices_.push_back(node_index);
}

void PooledLimitOrderBook::append_node(PriceLevel& level, std::size_t node_index) noexcept {
    auto& node = nodes_[node_index];
    node.prev = level.tail;
    node.next = npos;

    if (level.tail != npos) {
        nodes_[level.tail].next = node_index;
    } else {
        level.head = node_index;
    }

    level.tail = node_index;
    ++level.order_count;
}

void PooledLimitOrderBook::unlink_node(PriceLevel& level, std::size_t node_index) noexcept {
    const auto prev = nodes_[node_index].prev;
    const auto next = nodes_[node_index].next;

    if (prev != npos) {
        nodes_[prev].next = next;
    } else {
        level.head = next;
    }

    if (next != npos) {
        nodes_[next].prev = prev;
    } else {
        level.tail = prev;
    }

    --level.order_count;
}

void PooledLimitOrderBook::reset_free_list() {
    free_indices_.clear();
    for (auto& node : nodes_) {
        node = OrderNode{};
    }

    for (std::size_t index = nodes_.size(); index > 0; --index) {
        free_indices_.push_back(index - 1);
    }
}

PooledLimitOrderBook::PriceLevel* PooledLimitOrderBook::find_level(Side side, Price price) noexcept {
    if (side == Side::Buy) {
        const auto it = bid_levels_.find(price.ticks);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }

    const auto it = ask_levels_.find(price.ticks);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

const PooledLimitOrderBook::PriceLevel* PooledLimitOrderBook::find_level(Side side, Price price) const noexcept {
    if (side == Side::Buy) {
        const auto it = bid_levels_.find(price.ticks);
        return it == bid_levels_.end() ? nullptr : &it->second;
    }

    const auto it = ask_levels_.find(price.ticks);
    return it == ask_levels_.end() ? nullptr : &it->second;
}

PriceLevelView PooledLimitOrderBook::make_price_level_view(Price::rep price, const PriceLevel& level) noexcept {
    return PriceLevelView{
        Price{price},
        level.total_quantity,
        level.order_count,
    };
}

void PooledLimitOrderBook::remove_existing_order(OrderLookup::iterator order_it) {
    const auto record = order_it->second;

    if (record.side == Side::Buy) {
        auto level_it = bid_levels_.find(record.price.ticks);
        auto& level = level_it->second;
        level.total_quantity.units -= record.quantity.units;
        unlink_node(level, record.node_index);
        release_node(record.node_index);
        if (level.order_count == 0) {
            bid_levels_.erase(level_it);
        }
    } else {
        auto level_it = ask_levels_.find(record.price.ticks);
        auto& level = level_it->second;
        level.total_quantity.units -= record.quantity.units;
        unlink_node(level, record.node_index);
        release_node(record.node_index);
        if (level.order_count == 0) {
            ask_levels_.erase(level_it);
        }
    }

    order_lookup_.erase(order_it);
}

}  // namespace datagine
