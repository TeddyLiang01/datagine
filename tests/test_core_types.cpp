#include "datagine/book/limit_order_book.hpp"
#include "datagine/core/market_event.hpp"

#include <cassert>

int main() {
    using namespace datagine;

    const Price price{10125};
    const Quantity quantity{250};
    const OrderId order_id{42};
    const Timestamp timestamp{1'000'000};

    assert(price.ticks == 10125);
    assert(quantity.units == 250);
    assert(order_id.value == 42);
    assert(timestamp.nanoseconds == 1'000'000);

    const MarketEvent event{
        EventType::Add,
        timestamp,
        order_id,
        Side::Buy,
        price,
        quantity,
    };

    assert(event.type == EventType::Add);
    assert(event.side == Side::Buy);
    assert(to_string(event.type) == "add");
    assert(to_string(event.side) == "buy");

    LimitOrderBook book;
    assert(book.validate_invariants());
    assert(book.events_applied() == 0);

    assert(book.apply(event) == BookResult::Applied);
    assert(book.events_applied() == 1);
    assert(book.stats().events_applied == 1);
    assert(book.validate_invariants());

    book.clear();
    assert(book.events_applied() == 0);

    return 0;
}
