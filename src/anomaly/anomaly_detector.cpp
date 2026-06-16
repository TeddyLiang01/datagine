#include "datagine/anomaly/anomaly_detector.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace datagine {

namespace {

constexpr double kVarianceFloor = 1e-9;

std::uint64_t sum_depth(const std::vector<PriceLevelView>& levels) noexcept {
    std::uint64_t total = 0;
    for (const auto& level : levels) {
        total += level.quantity.units;
    }
    return total;
}

bool is_cancel(EventType type) noexcept {
    return type == EventType::Cancel;
}

bool is_add(EventType type) noexcept {
    return type == EventType::Add;
}

}  // namespace

FeatureExtractor::FeatureExtractor(AnomalyConfig config)
    : config_(config) {}

MicrostructureFeatures FeatureExtractor::extract(
    const MarketEvent& event,
    const LimitOrderBook& book) {
    expire_event_windows(event.timestamp);
    expire_mid_window(event.timestamp);

    message_window_.push_back(EventWindowEntry{event.timestamp, event.type});
    cancel_ratio_window_.push_back(EventWindowEntry{event.timestamp, event.type});

    MicrostructureFeatures features{};
    features.timestamp = event.timestamp;
    features.message_rate = message_rate();
    features.cancel_add_ratio = cancel_add_ratio();

    const auto depth = book.depth(config_.top_n_levels);
    features.top_n_bid_depth = sum_depth(depth.bids);
    features.top_n_ask_depth = sum_depth(depth.asks);

    const auto top = book.top_of_book();
    if (top.bid.has_value() && top.ask.has_value()) {
        features.has_two_sided_book = true;
        const auto bid = static_cast<double>(top.bid->price.ticks);
        const auto ask = static_cast<double>(top.ask->price.ticks);
        features.bid_ask_spread = ask - bid;
        features.mid_price = (bid + ask) / 2.0;

        const auto total_depth = features.top_n_bid_depth + features.top_n_ask_depth;
        if (total_depth > 0) {
            features.order_book_imbalance =
                (static_cast<double>(features.top_n_bid_depth) -
                 static_cast<double>(features.top_n_ask_depth)) /
                static_cast<double>(total_depth);
        }

        mid_price_window_.push_back(MidPriceEntry{event.timestamp, *features.mid_price});
        features.mid_price_volatility = mid_volatility();
    }

    return features;
}

void FeatureExtractor::reset() {
    message_window_.clear();
    cancel_ratio_window_.clear();
    mid_price_window_.clear();
}

double FeatureExtractor::window_seconds(Timestamp::rep window_ns) noexcept {
    return static_cast<double>(window_ns) / 1'000'000'000.0;
}

void FeatureExtractor::expire_event_windows(Timestamp timestamp) {
    const auto expire = [](auto& window, Timestamp now, Timestamp::rep window_ns) {
        while (!window.empty() && now.nanoseconds - window.front().timestamp.nanoseconds > window_ns) {
            window.pop_front();
        }
    };

    expire(message_window_, timestamp, config_.message_window_ns);
    expire(cancel_ratio_window_, timestamp, config_.cancel_ratio_window_ns);
}

void FeatureExtractor::expire_mid_window(Timestamp timestamp) {
    while (!mid_price_window_.empty() &&
           timestamp.nanoseconds - mid_price_window_.front().timestamp.nanoseconds > config_.volatility_window_ns) {
        mid_price_window_.pop_front();
    }
}

double FeatureExtractor::message_rate() const noexcept {
    const auto seconds = window_seconds(config_.message_window_ns);
    return seconds > 0.0 ? static_cast<double>(message_window_.size()) / seconds : 0.0;
}

double FeatureExtractor::cancel_add_ratio() const noexcept {
    std::size_t cancel_count = 0;
    std::size_t add_count = 0;

    for (const auto& entry : cancel_ratio_window_) {
        if (is_cancel(entry.type)) {
            ++cancel_count;
        } else if (is_add(entry.type)) {
            ++add_count;
        }
    }

    return static_cast<double>(cancel_count) / static_cast<double>(std::max<std::size_t>(1, add_count));
}

std::optional<double> FeatureExtractor::mid_volatility() const {
    if (mid_price_window_.size() < 2) {
        return std::nullopt;
    }

    double sum = 0.0;
    for (const auto& entry : mid_price_window_) {
        sum += entry.mid_price;
    }

    const auto mean = sum / static_cast<double>(mid_price_window_.size());
    double variance = 0.0;
    for (const auto& entry : mid_price_window_) {
        const auto delta = entry.mid_price - mean;
        variance += delta * delta;
    }

    variance /= static_cast<double>(mid_price_window_.size());
    return std::sqrt(variance);
}

EwmaZScoreDetector::EwmaZScoreDetector(AnomalyConfig config)
    : config_(config) {}

std::optional<double> EwmaZScoreDetector::score(std::string_view feature, double value) {
    auto& state = states_[std::string{feature}];
    std::optional<double> z_score{};

    if (state.samples >= config_.min_samples) {
        z_score = (value - state.mean) / std::sqrt(std::max(state.variance, kVarianceFloor));
    }

    if (state.samples == 0) {
        state.mean = value;
        state.variance = 0.0;
        state.samples = 1;
        return z_score;
    }

    const auto alpha = config_.ewma_alpha;
    const auto delta = value - state.mean;
    state.mean += alpha * delta;
    state.variance = (1.0 - alpha) * (state.variance + alpha * delta * delta);
    ++state.samples;
    return z_score;
}

void EwmaZScoreDetector::reset() {
    states_.clear();
}

AnomalyMonitor::AnomalyMonitor(AnomalyConfig config)
    : config_(config),
      extractor_(config),
      detector_(config) {}

std::vector<AnomalyEvent> AnomalyMonitor::observe(
    const MarketEvent& event,
    const LimitOrderBook& book) {
    const auto features = extractor_.extract(event, book);
    std::vector<AnomalyEvent> anomalies{};

    if (features.bid_ask_spread.has_value()) {
        maybe_emit(anomalies, event, "bid_ask_spread", "spread_widening", *features.bid_ask_spread);
    }

    if (features.has_two_sided_book) {
        maybe_emit(
            anomalies,
            event,
            "top_n_bid_depth",
            "bid_depth_collapse",
            static_cast<double>(features.top_n_bid_depth),
            ScoreDirection::Negative);
        maybe_emit(
            anomalies,
            event,
            "top_n_ask_depth",
            "ask_depth_collapse",
            static_cast<double>(features.top_n_ask_depth),
            ScoreDirection::Negative);
    }

    if (features.order_book_imbalance.has_value()) {
        maybe_emit(anomalies, event, "order_book_imbalance", "imbalance_spike", std::abs(*features.order_book_imbalance));
    }

    maybe_emit(anomalies, event, "message_rate", "message_rate_burst", features.message_rate);
    maybe_emit(anomalies, event, "cancel_add_ratio", "cancel_add_ratio_spike", features.cancel_add_ratio);

    if (features.mid_price_volatility.has_value()) {
        maybe_emit(anomalies, event, "mid_price_volatility", "mid_volatility_spike", *features.mid_price_volatility);
    }

    return anomalies;
}

void AnomalyMonitor::reset() {
    extractor_.reset();
    detector_.reset();
}

void AnomalyMonitor::maybe_emit(
    std::vector<AnomalyEvent>& output,
    const MarketEvent& event,
    std::string_view feature,
    std::string_view reason,
    double value,
    ScoreDirection direction) {
    const auto z_score = detector_.score(feature, value);
    if (!z_score.has_value()) {
        return;
    }

    const auto threshold = config_.z_score_threshold;
    bool passes = false;
    switch (direction) {
        case ScoreDirection::Positive:
            passes = *z_score >= threshold;
            break;
        case ScoreDirection::Negative:
            passes = *z_score <= -threshold;
            break;
        case ScoreDirection::Absolute:
            passes = std::abs(*z_score) >= threshold;
            break;
    }
    if (!passes) {
        return;
    }

    output.push_back(AnomalyEvent{
        event.timestamp,
        event.order_id,
        std::string{feature},
        std::string{reason},
        value,
        *z_score,
    });
}

std::map<std::string, std::size_t> summarize_anomaly_reasons(
    const std::vector<AnomalyEvent>& anomalies) {
    std::map<std::string, std::size_t> counts{};
    for (const auto& anomaly : anomalies) {
        ++counts[anomaly.reason];
    }
    return counts;
}

}  // namespace datagine
