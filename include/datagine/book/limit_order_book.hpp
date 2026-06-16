#pragma once

#include "datagine/core/market_event.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>
#include <string_view>

namespace datagine {

enum class BookResult : std::uint8_t {
    Applied,
    DuplicateOrderId,
    MissingOrderId,
    InvalidQuantity,
    InsufficientQuantity,
    UnsupportedModify,
    CapacityExceeded,
};

[[nodiscard]] std::string_view to_string(BookResult result) noexcept;

struct PriceLevelView {
    Price price{};
    Quantity quantity{};
    std::size_t order_count{};

    bool operator==(const PriceLevelView&) const = default;
};

struct TopOfBook {
    std::optional<PriceLevelView> bid{};
    std::optional<PriceLevelView> ask{};

    bool operator==(const TopOfBook&) const = default;
};

struct BookDepth {
    std::vector<PriceLevelView> bids{};
    std::vector<PriceLevelView> asks{};

    bool operator==(const BookDepth&) const = default;
};

struct OrderView {
    OrderId order_id{};
    Timestamp timestamp{};
    Quantity quantity{};

    bool operator==(const OrderView&) const = default;
};

struct PriceLevelSnapshot {
    Price price{};
    Quantity quantity{};
    std::vector<OrderView> orders{};

    bool operator==(const PriceLevelSnapshot&) const = default;
};

struct BookSnapshot {
    std::vector<PriceLevelSnapshot> bids{};
    std::vector<PriceLevelSnapshot> asks{};

    bool operator==(const BookSnapshot&) const = default;
};

struct InvariantOptions {
    bool allow_crossed_book{false};
};

class LimitOrderBook {
public:
    struct Stats {
        std::uint64_t events_applied{};
    };

    [[nodiscard]] BookResult add_order(
        OrderId order_id,
        Side side,
        Price price,
        Quantity quantity,
        Timestamp timestamp);
    [[nodiscard]] BookResult cancel_order(OrderId order_id);
    [[nodiscard]] BookResult modify_order(OrderId order_id, Quantity new_quantity);
    [[nodiscard]] BookResult execute_order(OrderId order_id, Quantity executed_quantity);
    [[nodiscard]] BookResult apply(const MarketEvent& event);

    void clear() noexcept;

    [[nodiscard]] TopOfBook top_of_book() const;
    [[nodiscard]] BookDepth depth(std::size_t levels) const;
    [[nodiscard]] BookSnapshot snapshot() const;
    [[nodiscard]] std::vector<OrderId> order_ids_at_price(Side side, Price price) const;
    [[nodiscard]] bool validate_invariants(InvariantOptions options = {}) const;
    [[nodiscard]] const Stats& stats() const noexcept;
    [[nodiscard]] std::uint64_t events_applied() const noexcept;
    [[nodiscard]] std::size_t live_order_count() const noexcept;

private:
    struct OrderEntry {
        OrderId order_id{};
        Timestamp timestamp{};
        Quantity quantity{};
    };

    using OrderQueue = std::list<OrderEntry>;

    struct PriceLevel {
        Quantity total_quantity{};
        OrderQueue orders{};
    };

    using BidLevels = std::map<Price::rep, PriceLevel, std::greater<>>;
    using AskLevels = std::map<Price::rep, PriceLevel, std::less<>>;
    using OrderIterator = OrderQueue::iterator;

    struct OrderRecord {
        Side side{};
        Price price{};
        Timestamp timestamp{};
        Quantity quantity{};
        OrderIterator iterator{};
    };

    using OrderLookup = std::unordered_map<OrderId::rep, OrderRecord>;

    [[nodiscard]] PriceLevel* find_level(Side side, Price price) noexcept;
    [[nodiscard]] const PriceLevel* find_level(Side side, Price price) const noexcept;
    [[nodiscard]] static PriceLevelView make_price_level_view(
        Price::rep price,
        const PriceLevel& level) noexcept;

    void remove_existing_order(OrderLookup::iterator order_it) noexcept;

    BidLevels bid_levels_{};
    AskLevels ask_levels_{};
    OrderLookup order_lookup_{};
    Stats stats_{};
};

}  // namespace datagine
