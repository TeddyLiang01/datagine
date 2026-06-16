#pragma once

#include "datagine/book/limit_order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace datagine {

struct AnomalyConfig {
    std::size_t top_n_levels{5};
    Timestamp::rep message_window_ns{1'000'000'000};
    Timestamp::rep cancel_ratio_window_ns{1'000'000'000};
    Timestamp::rep volatility_window_ns{100'000'000};
    double ewma_alpha{0.05};
    double z_score_threshold{4.0};
    std::size_t min_samples{20};
};

struct MicrostructureFeatures {
    Timestamp timestamp{};
    bool has_two_sided_book{false};
    std::optional<double> bid_ask_spread{};
    std::optional<double> mid_price{};
    std::uint64_t top_n_bid_depth{};
    std::uint64_t top_n_ask_depth{};
    std::optional<double> order_book_imbalance{};
    double message_rate{};
    double cancel_add_ratio{};
    std::optional<double> mid_price_volatility{};
};

struct AnomalyEvent {
    Timestamp timestamp{};
    OrderId order_id{};
    std::string feature{};
    std::string reason{};
    double value{};
    double z_score{};
};

class FeatureExtractor {
public:
    explicit FeatureExtractor(AnomalyConfig config = {});

    [[nodiscard]] MicrostructureFeatures extract(
        const MarketEvent& event,
        const LimitOrderBook& book);

    void reset();

private:
    struct EventWindowEntry {
        Timestamp timestamp{};
        EventType type{EventType::Add};
    };

    struct MidPriceEntry {
        Timestamp timestamp{};
        double mid_price{};
    };

    [[nodiscard]] static double window_seconds(Timestamp::rep window_ns) noexcept;
    void expire_event_windows(Timestamp timestamp);
    void expire_mid_window(Timestamp timestamp);
    [[nodiscard]] double message_rate() const noexcept;
    [[nodiscard]] double cancel_add_ratio() const noexcept;
    [[nodiscard]] std::optional<double> mid_volatility() const;

    AnomalyConfig config_{};
    std::deque<EventWindowEntry> message_window_{};
    std::deque<EventWindowEntry> cancel_ratio_window_{};
    std::deque<MidPriceEntry> mid_price_window_{};
};

class EwmaZScoreDetector {
public:
    explicit EwmaZScoreDetector(AnomalyConfig config = {});

    [[nodiscard]] std::optional<double> score(std::string_view feature, double value);
    void reset();

private:
    struct State {
        double mean{};
        double variance{};
        std::size_t samples{};
    };

    AnomalyConfig config_{};
    std::unordered_map<std::string, State> states_{};
};

class AnomalyMonitor {
public:
    explicit AnomalyMonitor(AnomalyConfig config = {});

    [[nodiscard]] std::vector<AnomalyEvent> observe(
        const MarketEvent& event,
        const LimitOrderBook& book);

    void reset();

private:
    enum class ScoreDirection : std::uint8_t {
        Positive,
        Negative,
        Absolute,
    };

    void maybe_emit(
        std::vector<AnomalyEvent>& output,
        const MarketEvent& event,
        std::string_view feature,
        std::string_view reason,
        double value,
        ScoreDirection direction = ScoreDirection::Positive);

    AnomalyConfig config_{};
    FeatureExtractor extractor_;
    EwmaZScoreDetector detector_;
};

[[nodiscard]] std::map<std::string, std::size_t> summarize_anomaly_reasons(
    const std::vector<AnomalyEvent>& anomalies);

}  // namespace datagine
