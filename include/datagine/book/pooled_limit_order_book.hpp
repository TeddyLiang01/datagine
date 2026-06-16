#pragma once

#include "datagine/book/limit_order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace datagine {

class PooledLimitOrderBook {
public:
    struct Stats {
        std::uint64_t events_applied{};
    };

    explicit PooledLimitOrderBook(std::size_t max_live_orders);

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

    void clear();

    [[nodiscard]] TopOfBook top_of_book() const;
    [[nodiscard]] BookDepth depth(std::size_t levels) const;
    [[nodiscard]] BookSnapshot snapshot() const;
    [[nodiscard]] std::vector<OrderId> order_ids_at_price(Side side, Price price) const;
    [[nodiscard]] bool validate_invariants(InvariantOptions options = {}) const;
    [[nodiscard]] const Stats& stats() const noexcept;
    [[nodiscard]] std::uint64_t events_applied() const noexcept;
    [[nodiscard]] std::size_t live_order_count() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    struct OrderNode {
        OrderId order_id{};
        Timestamp timestamp{};
        Quantity quantity{};
        std::size_t prev{npos};
        std::size_t next{npos};
        bool active{false};
    };

    struct PriceLevel {
        Quantity total_quantity{};
        std::size_t order_count{};
        std::size_t head{npos};
        std::size_t tail{npos};
    };

    using BidLevels = std::map<Price::rep, PriceLevel, std::greater<>>;
    using AskLevels = std::map<Price::rep, PriceLevel, std::less<>>;

    struct OrderRecord {
        Side side{};
        Price price{};
        Timestamp timestamp{};
        Quantity quantity{};
        std::size_t node_index{npos};
    };

    using OrderLookup = std::unordered_map<OrderId::rep, OrderRecord>;

    [[nodiscard]] std::optional<std::size_t> allocate_node() noexcept;
    void release_node(std::size_t node_index) noexcept;
    void append_node(PriceLevel& level, std::size_t node_index) noexcept;
    void unlink_node(PriceLevel& level, std::size_t node_index) noexcept;
    void reset_free_list();

    [[nodiscard]] PriceLevel* find_level(Side side, Price price) noexcept;
    [[nodiscard]] const PriceLevel* find_level(Side side, Price price) const noexcept;
    [[nodiscard]] static PriceLevelView make_price_level_view(
        Price::rep price,
        const PriceLevel& level) noexcept;

    void remove_existing_order(OrderLookup::iterator order_it);

    std::vector<OrderNode> nodes_{};
    std::vector<std::size_t> free_indices_{};
    BidLevels bid_levels_{};
    AskLevels ask_levels_{};
    OrderLookup order_lookup_{};
    Stats stats_{};
};

}  // namespace datagine
