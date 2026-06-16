#include "datagine/book/limit_order_book.hpp"
#include "datagine/book/pooled_limit_order_book.hpp"

#include <cassert>
#include <vector>

namespace {

using namespace datagine;

MarketEvent event(
    EventType type,
    OrderId::rep id,
    Side side,
    Price::rep price,
    Quantity::rep quantity,
    Timestamp::rep timestamp) {
    return MarketEvent{
        type,
        Timestamp{timestamp},
        OrderId{id},
        side,
        Price{price},
        Quantity{quantity},
    };
}

void assert_level(
    const PriceLevelView& level,
    Price::rep price,
    Quantity::rep quantity,
    std::size_t order_count) {
    assert(level.price == Price{price});
    assert(level.quantity == Quantity{quantity});
    assert(level.order_count == order_count);
}

void pooled_basic_operations_match_baseline() {
    const std::vector<MarketEvent> events{
        event(EventType::Add, 1, Side::Buy, 100, 10, 1),
        event(EventType::Add, 2, Side::Buy, 100, 8, 2),
        event(EventType::Add, 3, Side::Sell, 101, 7, 3),
        event(EventType::Modify, 1, Side::Buy, 100, 6, 4),
        event(EventType::Execute, 3, Side::Sell, 101, 2, 5),
        event(EventType::Cancel, 2, Side::Buy, 0, 0, 6),
        event(EventType::Add, 4, Side::Sell, 102, 4, 7),
    };

    LimitOrderBook baseline;
    PooledLimitOrderBook pooled{8};

    for (const auto& item : events) {
        assert(baseline.apply(item) == BookResult::Applied);
        assert(pooled.apply(item) == BookResult::Applied);
    }

    assert(pooled.validate_invariants());
    assert(baseline.top_of_book() == pooled.top_of_book());
    assert(baseline.depth(8) == pooled.depth(8));
    assert(baseline.snapshot() == pooled.snapshot());
    assert(baseline.order_ids_at_price(Side::Buy, Price{100}) == pooled.order_ids_at_price(Side::Buy, Price{100}));
    assert(baseline.order_ids_at_price(Side::Sell, Price{101}) == pooled.order_ids_at_price(Side::Sell, Price{101}));
    assert(baseline.live_order_count() == pooled.live_order_count());
}

void capacity_exceeded_does_not_mutate_state() {
    PooledLimitOrderBook book{1};

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    const auto before_top = book.top_of_book();
    const auto before_snapshot = book.snapshot();
    const auto before_events = book.events_applied();

    assert(book.add_order(OrderId{2}, Side::Sell, Price{101}, Quantity{5}, Timestamp{2}) == BookResult::CapacityExceeded);
    assert(book.top_of_book() == before_top);
    assert(book.snapshot() == before_snapshot);
    assert(book.events_applied() == before_events);
    assert(book.live_order_count() == 1);
    assert(book.validate_invariants());
}

void freed_nodes_are_reused_after_cancel_and_execute() {
    PooledLimitOrderBook cancel_book{1};

    assert(cancel_book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(cancel_book.cancel_order(OrderId{1}) == BookResult::Applied);
    assert(cancel_book.add_order(OrderId{2}, Side::Sell, Price{101}, Quantity{5}, Timestamp{2}) == BookResult::Applied);
    assert(cancel_book.live_order_count() == 1);
    assert(cancel_book.validate_invariants());

    const auto cancel_top = cancel_book.top_of_book();
    assert(cancel_top.ask.has_value());
    assert_level(*cancel_top.ask, 101, 5, 1);

    PooledLimitOrderBook execute_book{1};

    assert(execute_book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(execute_book.execute_order(OrderId{1}, Quantity{10}) == BookResult::Applied);
    assert(execute_book.add_order(OrderId{2}, Side::Sell, Price{101}, Quantity{5}, Timestamp{2}) == BookResult::Applied);
    assert(execute_book.live_order_count() == 1);
    assert(execute_book.validate_invariants());
}

void price_time_priority_is_fifo() {
    PooledLimitOrderBook book{4};

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{30}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{100}, Quantity{10}, Timestamp{10}) == BookResult::Applied);
    assert(book.add_order(OrderId{3}, Side::Buy, Price{100}, Quantity{10}, Timestamp{20}) == BookResult::Applied);

    assert(book.order_ids_at_price(Side::Buy, Price{100}) == std::vector<OrderId>({OrderId{1}, OrderId{2}, OrderId{3}}));
    assert(book.validate_invariants());
}

void crossed_book_invariant_matches_baseline_behavior() {
    PooledLimitOrderBook book{2};

    assert(book.add_order(OrderId{1}, Side::Sell, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{101}, Quantity{10}, Timestamp{2}) == BookResult::Applied);

    assert(!book.validate_invariants());
    assert(book.validate_invariants(InvariantOptions{.allow_crossed_book = true}));
}

}  // namespace

int main() {
    pooled_basic_operations_match_baseline();
    capacity_exceeded_does_not_mutate_state();
    freed_nodes_are_reused_after_cancel_and_execute();
    price_time_priority_is_fifo();
    crossed_book_invariant_matches_baseline_behavior();

    return 0;
}
