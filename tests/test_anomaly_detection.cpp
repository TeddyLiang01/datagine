#include "datagine/anomaly/anomaly_detector.hpp"
#include "datagine/replay/replay_engine.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
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

AnomalyConfig sensitive_config() {
    AnomalyConfig config{};
    config.top_n_levels = 5;
    config.message_window_ns = 10;
    config.cancel_ratio_window_ns = 10;
    config.volatility_window_ns = 200;
    config.ewma_alpha = 0.20;
    config.z_score_threshold = 3.0;
    config.min_samples = 5;
    return config;
}

std::filesystem::path write_fixture(std::string name, const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / std::move(name);
    std::ofstream output{path};
    output << content;
    return path;
}

bool contains_reason(const std::vector<AnomalyEvent>& anomalies, const std::string& reason) {
    for (const auto& anomaly : anomalies) {
        if (anomaly.reason == reason) {
            return true;
        }
    }
    return false;
}

std::vector<AnomalyEvent> run_events(
    const std::vector<MarketEvent>& events,
    AnomalyConfig config = sensitive_config()) {
    LimitOrderBook book;
    AnomalyMonitor monitor{config};
    std::vector<AnomalyEvent> anomalies{};

    for (const auto& item : events) {
        assert(book.apply(item) == BookResult::Applied);
        auto emitted = monitor.observe(item, book);
        anomalies.insert(anomalies.end(), emitted.begin(), emitted.end());
    }

    return anomalies;
}

std::vector<MarketEvent> stable_two_sided_prefix(OrderId::rep& next_id, Timestamp::rep& timestamp) {
    std::vector<MarketEvent> events{};
    events.push_back(event(EventType::Add, next_id++, Side::Buy, 100, 100, timestamp++));
    events.push_back(event(EventType::Add, next_id++, Side::Sell, 101, 100, timestamp++));

    for (int i = 0; i < 12; ++i) {
        timestamp += 20;
        events.push_back(event(EventType::Modify, 1, Side::Buy, 100, 100, timestamp));
    }

    return events;
}

void normal_market_has_no_anomalies_with_conservative_threshold() {
    auto config = sensitive_config();
    config.z_score_threshold = 100.0;

    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;
    auto events = stable_two_sided_prefix(next_id, timestamp);
    for (int i = 0; i < 20; ++i) {
        timestamp += 20;
        events.push_back(event(EventType::Modify, 2, Side::Sell, 101, 100, timestamp));
    }

    const auto anomalies = run_events(events, config);
    assert(anomalies.empty());
}

void sudden_spread_widening_is_detected() {
    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;
    auto events = stable_two_sided_prefix(next_id, timestamp);

    timestamp += 20;
    events.push_back(event(EventType::Cancel, 2, Side::Sell, 0, 0, timestamp));
    timestamp += 20;
    events.push_back(event(EventType::Add, next_id++, Side::Sell, 120, 100, timestamp));

    const auto anomalies = run_events(events);
    assert(contains_reason(anomalies, "spread_widening"));
}

void cancel_burst_is_detected() {
    std::vector<MarketEvent> events{};
    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;

    events.push_back(event(EventType::Add, next_id++, Side::Sell, 101, 100, timestamp));
    for (int i = 0; i < 20; ++i) {
        timestamp += 20;
        events.push_back(event(EventType::Add, next_id++, Side::Buy, 100, 10, timestamp));
    }

    timestamp += 1'000;
    for (OrderId::rep id = 2; id < 10; ++id) {
        events.push_back(event(EventType::Cancel, id, Side::Buy, 0, 0, timestamp++));
    }

    const auto anomalies = run_events(events);
    assert(contains_reason(anomalies, "cancel_add_ratio_spike"));
}

void message_rate_burst_is_detected() {
    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;
    auto events = stable_two_sided_prefix(next_id, timestamp);

    timestamp += 1'000;
    for (int i = 0; i < 16; ++i) {
        events.push_back(event(EventType::Modify, 1, Side::Buy, 100, 100, timestamp++));
    }

    const auto anomalies = run_events(events);
    assert(contains_reason(anomalies, "message_rate_burst"));
}

void one_sided_depth_collapse_is_detected() {
    std::vector<MarketEvent> events{};
    OrderId::rep next_id = 1;
    Timestamp::rep timestamp = 0;

    for (int i = 0; i < 5; ++i) {
        events.push_back(event(EventType::Add, next_id++, Side::Buy, 100, 100, timestamp++));
    }
    events.push_back(event(EventType::Add, next_id++, Side::Sell, 101, 100, timestamp++));

    for (int i = 0; i < 12; ++i) {
        timestamp += 20;
        events.push_back(event(EventType::Modify, 6, Side::Sell, 101, 100, timestamp));
    }

    timestamp += 20;
    events.push_back(event(EventType::Cancel, 1, Side::Buy, 0, 0, timestamp++));
    events.push_back(event(EventType::Cancel, 2, Side::Buy, 0, 0, timestamp++));
    events.push_back(event(EventType::Cancel, 3, Side::Buy, 0, 0, timestamp++));

    const auto anomalies = run_events(events);
    assert(contains_reason(anomalies, "bid_depth_collapse"));
}

void replay_anomaly_option_controls_monitoring() {
    const auto path = write_fixture(
        "datagine_anomaly_replay.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "0,ADD,1,B,100,100\n"
        "1,ADD,2,S,101,100\n"
        "20,MODIFY,1,,100,100\n"
        "40,MODIFY,1,,100,100\n"
        "60,MODIFY,1,,100,100\n"
        "80,MODIFY,1,,100,100\n"
        "100,MODIFY,1,,100,100\n"
        "120,MODIFY,1,,100,100\n"
        "140,CANCEL,2,,,\n"
        "160,ADD,3,S,120,100\n");

    ReplayEngine disabled_engine;
    const auto disabled = disabled_engine.replay_file(path);
    assert(disabled.anomalies.empty());
    assert(disabled.anomaly_reason_counts.empty());

    ReplayOptions options{};
    options.enable_anomaly_detection = true;
    options.anomaly_config = sensitive_config();

    ReplayEngine enabled_engine;
    const auto enabled = enabled_engine.replay_file(path, options);
    assert(!enabled.anomalies.empty());
    assert(enabled.anomaly_reason_counts.count("spread_widening") == 1);
}

}  // namespace

int main() {
    normal_market_has_no_anomalies_with_conservative_threshold();
    sudden_spread_widening_is_detected();
    cancel_burst_is_detected();
    message_rate_burst_is_detected();
    one_sided_depth_collapse_is_detected();
    replay_anomaly_option_controls_monitoring();

    return 0;
}
