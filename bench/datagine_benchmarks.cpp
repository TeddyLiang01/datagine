#include "datagine/book/limit_order_book.hpp"
#include "datagine/book/pooled_limit_order_book.hpp"
#include "datagine/anomaly/anomaly_detector.hpp"
#include "datagine/core/market_event.hpp"
#include "datagine/feed/csv_parser.hpp"
#include "datagine/queue/spsc_ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace datagine;

constexpr auto kDefaultBatchSize = 4096;
constexpr auto kLargeBatchSize = 16384;

MarketEvent make_event(
    EventType type,
    OrderId::rep order_id,
    Side side,
    Price::rep price,
    Quantity::rep quantity,
    Timestamp::rep timestamp) {
    return MarketEvent{
        type,
        Timestamp{timestamp},
        OrderId{order_id},
        side,
        Price{price},
        Quantity{quantity},
    };
}

std::vector<MarketEvent> make_mixed_stream(std::size_t cycles) {
    std::vector<MarketEvent> events{};
    events.reserve(cycles * 3);

    for (std::size_t i = 0; i < cycles; ++i) {
        const auto order_id = static_cast<OrderId::rep>(i + 1);
        const auto timestamp = static_cast<Timestamp::rep>(i * 3);
        const auto price = static_cast<Price::rep>(10000 + (i % 64));

        events.push_back(make_event(EventType::Add, order_id, Side::Buy, price, 100, timestamp));
        events.push_back(make_event(EventType::Execute, order_id, Side::Buy, price, 40, timestamp + 1));
        events.push_back(make_event(EventType::Cancel, order_id, Side::Buy, 0, 0, timestamp + 2));
    }

    return events;
}

std::vector<MarketEvent> make_feature_stream(std::size_t cycles) {
    std::vector<MarketEvent> events{};
    events.reserve(2 + cycles * 4);

    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;
    events.push_back(make_event(EventType::Add, next_id++, Side::Buy, 10000, 1000, timestamp++));
    events.push_back(make_event(EventType::Add, next_id++, Side::Sell, 10005, 1000, timestamp++));

    for (std::size_t i = 0; i < cycles; ++i) {
        const auto temp_id = next_id++;
        events.push_back(make_event(EventType::Add, temp_id, Side::Buy, 9995, 10, timestamp++));
        events.push_back(make_event(EventType::Cancel, temp_id, Side::Buy, 0, 0, timestamp++));
        events.push_back(make_event(EventType::Modify, 1, Side::Buy, 10000, 1000, timestamp++));
        events.push_back(make_event(EventType::Modify, 2, Side::Sell, 10005, 1000, timestamp++));
    }

    return events;
}

std::filesystem::path write_csv_fixture(std::size_t rows) {
    const auto path = std::filesystem::temp_directory_path() / "datagine_benchmark_fixture.csv";
    std::ofstream output{path};

    output << "timestamp_ns,event_type,order_id,side,price,quantity\n";
    for (std::size_t i = 0; i < rows; ++i) {
        const auto order_id = static_cast<OrderId::rep>(i + 1);
        const auto timestamp = static_cast<Timestamp::rep>(100000000 + i);
        const auto side = (i % 2 == 0) ? "B" : "S";
        const auto price = (i % 2 == 0)
            ? static_cast<Price::rep>(10000 - (i % 64))
            : static_cast<Price::rep>(10025 + (i % 64));
        output << timestamp << ",ADD," << order_id << ',' << side << ',' << price << ",100\n";
    }

    return path;
}

void set_rate_counter(benchmark::State& state, const char* name, std::int64_t count) {
    state.SetItemsProcessed(count);
    state.counters[name] = benchmark::Counter(
        static_cast<double>(count),
        benchmark::Counter::kIsRate);
}

void BM_BaselineAddOrderHotPath(benchmark::State& state) {
    const auto batch_size = static_cast<std::size_t>(state.range(0));
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.add_order(
                OrderId{i + 1},
                Side::Buy,
                Price{static_cast<Price::rep>(10000 + (i % 128))},
                Quantity{100},
                Timestamp{static_cast<Timestamp::rep>(i)});
            if (result != BookResult::Applied) {
                state.SkipWithError("add_order returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "orders/s",
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_PooledAddOrderHotPath(benchmark::State& state) {
    const auto batch_size = static_cast<std::size_t>(state.range(0));
    PooledLimitOrderBook book{batch_size};

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.add_order(
                OrderId{i + 1},
                Side::Buy,
                Price{static_cast<Price::rep>(10000 + (i % 128))},
                Quantity{100},
                Timestamp{static_cast<Timestamp::rep>(i)});
            if (result != BookResult::Applied) {
                state.SkipWithError("pooled add_order returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "orders/s",
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_BaselineCancelOrderHotPath(benchmark::State& state) {
    const auto batch_size = static_cast<std::size_t>(state.range(0));
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.add_order(
                OrderId{i + 1},
                Side::Sell,
                Price{static_cast<Price::rep>(10100 + (i % 128))},
                Quantity{100},
                Timestamp{static_cast<Timestamp::rep>(i)});
            if (result != BookResult::Applied) {
                state.SkipWithError("benchmark setup add_order failed");
                break;
            }
        }
        state.ResumeTiming();

        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.cancel_order(OrderId{i + 1});
            if (result != BookResult::Applied) {
                state.SkipWithError("cancel_order returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "orders/s",
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_PooledCancelOrderHotPath(benchmark::State& state) {
    const auto batch_size = static_cast<std::size_t>(state.range(0));
    PooledLimitOrderBook book{batch_size};

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.add_order(
                OrderId{i + 1},
                Side::Sell,
                Price{static_cast<Price::rep>(10100 + (i % 128))},
                Quantity{100},
                Timestamp{static_cast<Timestamp::rep>(i)});
            if (result != BookResult::Applied) {
                state.SkipWithError("pooled benchmark setup add_order failed");
                break;
            }
        }
        state.ResumeTiming();

        for (std::size_t i = 0; i < batch_size; ++i) {
            const auto result = book.cancel_order(OrderId{i + 1});
            if (result != BookResult::Applied) {
                state.SkipWithError("pooled cancel_order returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "orders/s",
        state.iterations() * static_cast<std::int64_t>(batch_size));
}

void BM_BaselineMixedAddCancelExecuteStream(benchmark::State& state) {
    const auto cycles = static_cast<std::size_t>(state.range(0));
    const auto events = make_mixed_stream(cycles);
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& event : events) {
            const auto result = book.apply(event);
            if (result != BookResult::Applied) {
                state.SkipWithError("mixed stream apply returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.events_applied());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(events.size()));
}

void BM_PooledMixedAddCancelExecuteStream(benchmark::State& state) {
    const auto cycles = static_cast<std::size_t>(state.range(0));
    const auto events = make_mixed_stream(cycles);
    PooledLimitOrderBook book{cycles};

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& event : events) {
            const auto result = book.apply(event);
            if (result != BookResult::Applied) {
                state.SkipWithError("pooled mixed stream apply returned a non-applied result");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.events_applied());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(events.size()));
}

void BM_BaselineCsvParseApply(benchmark::State& state) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    const auto fixture_path = write_csv_fixture(rows);
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        CsvParser parser{fixture_path};

        if (!parser.is_open()) {
            state.SkipWithError("failed to open generated CSV fixture");
            break;
        }

        for (;;) {
            const auto parsed = parser.next();
            if (parsed.status == CsvParseStatus::EndOfFile) {
                break;
            }

            if (parsed.status != CsvParseStatus::Event || !parsed.event.has_value()) {
                state.SkipWithError("generated CSV fixture produced malformed row");
                break;
            }

            const auto result = book.apply(*parsed.event);
            if (result != BookResult::Applied) {
                state.SkipWithError("CSV parse/apply benchmark event was rejected by the book");
                break;
            }

            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(rows));
}

void BM_PooledCsvParseApply(benchmark::State& state) {
    const auto rows = static_cast<std::size_t>(state.range(0));
    const auto fixture_path = write_csv_fixture(rows);
    PooledLimitOrderBook book{rows};

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        CsvParser parser{fixture_path};

        if (!parser.is_open()) {
            state.SkipWithError("failed to open generated CSV fixture");
            break;
        }

        for (;;) {
            const auto parsed = parser.next();
            if (parsed.status == CsvParseStatus::EndOfFile) {
                break;
            }

            if (parsed.status != CsvParseStatus::Event || !parsed.event.has_value()) {
                state.SkipWithError("generated CSV fixture produced malformed row");
                break;
            }

            const auto result = book.apply(*parsed.event);
            if (result != BookResult::Applied) {
                state.SkipWithError("CSV parse/apply benchmark event was rejected by the pooled book");
                break;
            }

            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.live_order_count());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(rows));
}

void BM_DirectReplayInMemory(benchmark::State& state) {
    const auto cycles = static_cast<std::size_t>(state.range(0));
    const auto events = make_mixed_stream(cycles);
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        for (const auto& event : events) {
            const auto result = book.apply(event);
            if (result != BookResult::Applied) {
                state.SkipWithError("direct replay event was rejected by the book");
                break;
            }
            benchmark::DoNotOptimize(result);
        }

        benchmark::DoNotOptimize(book.events_applied());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(events.size()));
}

void BM_SpscQueuedReplayInMemory(benchmark::State& state) {
    const auto cycles = static_cast<std::size_t>(state.range(0));
    const auto events = make_mixed_stream(cycles);
    SpscRingBuffer queue{1024};
    LimitOrderBook book;

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        state.ResumeTiming();

        std::atomic<bool> failed{false};

        std::thread producer{[&events, &queue]() {
            for (const auto& event : events) {
                while (!queue.try_push(event)) {
                    std::this_thread::yield();
                }
            }
        }};

        std::thread consumer{[&book, &events, &failed, &queue]() {
            MarketEvent event{};
            std::size_t consumed = 0;
            while (consumed < events.size()) {
                if (!queue.try_pop(event)) {
                    std::this_thread::yield();
                    continue;
                }

                const auto result = book.apply(event);
                if (result != BookResult::Applied) {
                    failed.store(true, std::memory_order_relaxed);
                }
                ++consumed;
            }
        }};

        producer.join();
        consumer.join();

        if (failed.load(std::memory_order_relaxed)) {
            state.SkipWithError("SPSC replay event was rejected by the book");
            break;
        }

        benchmark::DoNotOptimize(book.events_applied());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(events.size()));
}

void BM_FeatureExtractionAnomalyScoring(benchmark::State& state) {
    const auto cycles = static_cast<std::size_t>(state.range(0));
    const auto events = make_feature_stream(cycles);
    LimitOrderBook book;
    AnomalyMonitor monitor{};

    for (auto _ : state) {
        state.PauseTiming();
        book.clear();
        monitor.reset();
        state.ResumeTiming();

        std::size_t anomaly_count = 0;
        for (const auto& event : events) {
            const auto result = book.apply(event);
            if (result != BookResult::Applied) {
                state.SkipWithError("feature/anomaly benchmark event was rejected by the book");
                break;
            }

            auto anomalies = monitor.observe(event, book);
            anomaly_count += anomalies.size();
            benchmark::DoNotOptimize(anomaly_count);
        }

        benchmark::DoNotOptimize(book.events_applied());
        benchmark::ClobberMemory();
    }

    set_rate_counter(
        state,
        "events/s",
        state.iterations() * static_cast<std::int64_t>(events.size()));
}

}  // namespace

BENCHMARK(BM_BaselineAddOrderHotPath)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_PooledAddOrderHotPath)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_BaselineCancelOrderHotPath)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_PooledCancelOrderHotPath)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_BaselineMixedAddCancelExecuteStream)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_PooledMixedAddCancelExecuteStream)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_BaselineCsvParseApply)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_PooledCsvParseApply)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_DirectReplayInMemory)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_SpscQueuedReplayInMemory)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
BENCHMARK(BM_FeatureExtractionAnomalyScoring)->Arg(kDefaultBatchSize)->Arg(kLargeBatchSize);
