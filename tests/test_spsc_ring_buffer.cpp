#include "datagine/queue/spsc_ring_buffer.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using namespace datagine;

MarketEvent make_event(OrderId::rep order_id) {
    return MarketEvent{
        EventType::Add,
        Timestamp{static_cast<Timestamp::rep>(order_id)},
        OrderId{order_id},
        Side::Buy,
        Price{10000 + static_cast<Price::rep>(order_id % 32)},
        Quantity{100},
    };
}

std::uint64_t checksum_event(const MarketEvent& event) {
    auto hash = 14695981039346656037ull;
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(event.timestamp.nanoseconds);
    mix(event.order_id.value);
    mix(static_cast<std::uint64_t>(event.price.ticks));
    mix(event.quantity.units);
    return hash;
}

void single_threaded_push_pop_preserves_fifo() {
    SpscRingBuffer queue{4};

    assert(queue.empty());
    assert(queue.capacity() == 4);
    assert(queue.try_push(make_event(1)));
    assert(queue.try_push(make_event(2)));
    assert(queue.approximate_size() == 2);

    MarketEvent out{};
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{1});
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{2});
    assert(queue.empty());
}

void full_queue_returns_false_and_preserves_entries() {
    SpscRingBuffer queue{2};

    assert(queue.try_push(make_event(1)));
    assert(queue.try_push(make_event(2)));
    assert(queue.full());
    assert(!queue.try_push(make_event(3)));

    MarketEvent out{};
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{1});
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{2});
    assert(!queue.try_pop(out));
}

void empty_queue_returns_false() {
    SpscRingBuffer queue{8};
    MarketEvent out{};

    assert(queue.empty());
    assert(!queue.try_pop(out));
}

void wraparound_preserves_order() {
    SpscRingBuffer queue{4};

    assert(queue.try_push(make_event(1)));
    assert(queue.try_push(make_event(2)));
    assert(queue.try_push(make_event(3)));
    assert(queue.try_push(make_event(4)));

    MarketEvent out{};
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{1});
    assert(queue.try_pop(out));
    assert(out.order_id == OrderId{2});

    assert(queue.try_push(make_event(5)));
    assert(queue.try_push(make_event(6)));
    assert(queue.full());

    const std::vector<OrderId> expected{OrderId{3}, OrderId{4}, OrderId{5}, OrderId{6}};
    for (const auto order_id : expected) {
        assert(queue.try_pop(out));
        assert(out.order_id == order_id);
    }

    assert(queue.empty());
}

void constructor_rejects_invalid_capacity() {
    try {
        SpscRingBuffer invalid{0};
        (void)invalid;
        assert(false);
    } catch (const std::invalid_argument&) {
    }

    try {
        SpscRingBuffer invalid{3};
        (void)invalid;
        assert(false);
    } catch (const std::invalid_argument&) {
    }
}

void producer_consumer_threaded_smoke_test() {
    constexpr std::uint64_t kEventCount = 10'000;
    SpscRingBuffer queue{64};

    std::uint64_t expected_checksum = 0;
    for (std::uint64_t id = 1; id <= kEventCount; ++id) {
        expected_checksum ^= checksum_event(make_event(id));
    }

    std::uint64_t consumed_count = 0;
    std::uint64_t observed_checksum = 0;

    std::thread producer{[&queue]() {
        for (std::uint64_t id = 1; id <= kEventCount; ++id) {
            const auto event = make_event(id);
            while (!queue.try_push(event)) {
                std::this_thread::yield();
            }
        }
    }};

    std::thread consumer{[&queue, &consumed_count, &observed_checksum]() {
        MarketEvent event{};
        while (consumed_count < kEventCount) {
            if (!queue.try_pop(event)) {
                std::this_thread::yield();
                continue;
            }

            ++consumed_count;
            assert(event.order_id.value == consumed_count);
            observed_checksum ^= checksum_event(event);
        }
    }};

    producer.join();
    consumer.join();

    assert(consumed_count == kEventCount);
    assert(observed_checksum == expected_checksum);
    assert(queue.empty());
}

}  // namespace

int main() {
    single_threaded_push_pop_preserves_fifo();
    full_queue_returns_false_and_preserves_entries();
    empty_queue_returns_false();
    wraparound_preserves_order();
    constructor_rejects_invalid_capacity();
    producer_consumer_threaded_smoke_test();

    return 0;
}
