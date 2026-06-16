#include "datagine/book/limit_order_book.hpp"

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

void add_bid_add_ask_updates_top_and_depth() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Sell, Price{101}, Quantity{7}, Timestamp{2}) == BookResult::Applied);

    const auto top = book.top_of_book();
    assert(top.bid.has_value());
    assert(top.ask.has_value());
    assert_level(*top.bid, 100, 10, 1);
    assert_level(*top.ask, 101, 7, 1);

    const auto depth = book.depth(4);
    assert(depth.bids.size() == 1);
    assert(depth.asks.size() == 1);
    assert_level(depth.bids[0], 100, 10, 1);
    assert_level(depth.asks[0], 101, 7, 1);
    assert(book.live_order_count() == 2);
    assert(book.validate_invariants());
}

void cancel_existing_order_removes_depth() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{100}, Quantity{5}, Timestamp{2}) == BookResult::Applied);
    assert(book.cancel_order(OrderId{1}) == BookResult::Applied);

    const auto depth = book.depth(1);
    assert(depth.bids.size() == 1);
    assert_level(depth.bids[0], 100, 5, 1);
    assert(book.order_ids_at_price(Side::Buy, Price{100}) == std::vector<OrderId>{OrderId{2}});
    assert(book.live_order_count() == 1);
    assert(book.validate_invariants());
}

void cancel_missing_order_preserves_state() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Sell, Price{101}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    const auto before_top = book.top_of_book();
    const auto before_depth = book.depth(4);
    const auto before_events = book.events_applied();

    assert(book.cancel_order(OrderId{999}) == BookResult::MissingOrderId);
    assert(book.top_of_book() == before_top);
    assert(book.depth(4) == before_depth);
    assert(book.events_applied() == before_events);
    assert(book.live_order_count() == 1);
    assert(book.validate_invariants());
}

void partial_execute_reduces_quantity() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Sell, Price{101}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.execute_order(OrderId{1}, Quantity{4}) == BookResult::Applied);

    const auto top = book.top_of_book();
    assert(top.ask.has_value());
    assert_level(*top.ask, 101, 6, 1);
    assert(book.live_order_count() == 1);
    assert(book.validate_invariants());
}

void full_execute_removes_order_and_empty_level() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Sell, Price{101}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.execute_order(OrderId{1}, Quantity{10}) == BookResult::Applied);

    const auto top = book.top_of_book();
    assert(!top.bid.has_value());
    assert(!top.ask.has_value());
    assert(book.depth(1).asks.empty());
    assert(book.live_order_count() == 0);
    assert(book.validate_invariants());
}

void modify_quantity_reduction_preserves_priority() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{100}, Quantity{8}, Timestamp{2}) == BookResult::Applied);
    assert(book.modify_order(OrderId{1}, Quantity{6}) == BookResult::Applied);

    const auto depth = book.depth(1);
    assert(depth.bids.size() == 1);
    assert_level(depth.bids[0], 100, 14, 2);
    assert(book.order_ids_at_price(Side::Buy, Price{100}) == std::vector<OrderId>({OrderId{1}, OrderId{2}}));
    assert(book.modify_order(OrderId{1}, Quantity{7}) == BookResult::UnsupportedModify);
    assert(book.validate_invariants());
}

void price_time_priority_preserves_fifo_order() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Buy, Price{100}, Quantity{10}, Timestamp{30}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{100}, Quantity{10}, Timestamp{10}) == BookResult::Applied);
    assert(book.add_order(OrderId{3}, Side::Buy, Price{100}, Quantity{10}, Timestamp{20}) == BookResult::Applied);

    const auto ids = book.order_ids_at_price(Side::Buy, Price{100});
    assert(ids == std::vector<OrderId>({OrderId{1}, OrderId{2}, OrderId{3}}));
    assert(book.validate_invariants());
}

void deterministic_replay_produces_identical_final_state() {
    const std::vector<MarketEvent> events{
        event(EventType::Add, 1, Side::Buy, 100, 10, 1),
        event(EventType::Add, 2, Side::Buy, 99, 5, 2),
        event(EventType::Add, 3, Side::Sell, 101, 7, 3),
        event(EventType::Modify, 1, Side::Buy, 100, 6, 4),
        event(EventType::Trade, 3, Side::Sell, 101, 2, 5),
        event(EventType::Cancel, 2, Side::Buy, 99, 0, 6),
        event(EventType::Add, 4, Side::Sell, 102, 4, 7),
    };

    LimitOrderBook first;
    LimitOrderBook second;

    for (const auto& item : events) {
        assert(first.apply(item) == BookResult::Applied);
        assert(second.apply(item) == BookResult::Applied);
    }

    assert(first.top_of_book() == second.top_of_book());
    assert(first.depth(8) == second.depth(8));
    assert(first.order_ids_at_price(Side::Buy, Price{100}) == second.order_ids_at_price(Side::Buy, Price{100}));
    assert(first.order_ids_at_price(Side::Sell, Price{101}) == second.order_ids_at_price(Side::Sell, Price{101}));
    assert(first.events_applied() == second.events_applied());
    assert(first.live_order_count() == second.live_order_count());
    assert(first.validate_invariants());
    assert(second.validate_invariants());
}

void crossed_book_invariant_is_configurable() {
    LimitOrderBook book;

    assert(book.add_order(OrderId{1}, Side::Sell, Price{100}, Quantity{10}, Timestamp{1}) == BookResult::Applied);
    assert(book.add_order(OrderId{2}, Side::Buy, Price{101}, Quantity{10}, Timestamp{2}) == BookResult::Applied);

    assert(!book.validate_invariants());
    assert(book.validate_invariants(InvariantOptions{.allow_crossed_book = true}));
}

}  // namespace

int main() {
    add_bid_add_ask_updates_top_and_depth();
    cancel_existing_order_removes_depth();
    cancel_missing_order_preserves_state();
    partial_execute_reduces_quantity();
    full_execute_removes_order_and_empty_level();
    modify_quantity_reduction_preserves_priority();
    price_time_priority_preserves_fifo_order();
    deterministic_replay_produces_identical_final_state();
    crossed_book_invariant_is_configurable();

    return 0;
}
