#pragma once

#include "datagine/anomaly/anomaly_detector.hpp"
#include "datagine/book/limit_order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace datagine {

enum class ReplayErrorType : std::uint8_t {
    ParseError,
    ApplyRejected,
    InvariantFailure,
    FileOpenFailure,
};

struct ReplayError {
    ReplayErrorType type{ReplayErrorType::ParseError};
    std::size_t line_number{};
    std::string message{};
    std::string raw_row{};
};

struct ReplayOptions {
    InvariantOptions invariant_options{};
    bool validate_after_apply{true};
    bool enable_anomaly_detection{false};
    AnomalyConfig anomaly_config{};
};

struct ReplayResult {
    std::size_t events_processed{};
    std::size_t rejected_events{};
    bool file_open_failed{false};
    bool invariant_failed{false};
    std::uint64_t final_checksum{};
    TopOfBook final_top{};
    BookSnapshot final_snapshot{};
    std::vector<ReplayError> errors{};
    std::vector<AnomalyEvent> anomalies{};
    std::map<std::string, std::size_t> anomaly_reason_counts{};
};

class ReplayEngine {
public:
    [[nodiscard]] ReplayResult replay_file(
        const std::filesystem::path& input_path,
        ReplayOptions options = {});

    [[nodiscard]] const LimitOrderBook& book() const noexcept;

private:
    LimitOrderBook book_{};
};

[[nodiscard]] std::uint64_t checksum_book(const LimitOrderBook& book);
[[nodiscard]] std::uint64_t checksum_snapshot(const BookSnapshot& snapshot);
[[nodiscard]] std::string format_checksum(std::uint64_t checksum);
[[nodiscard]] std::string format_top_of_book(const TopOfBook& top);

}  // namespace datagine
