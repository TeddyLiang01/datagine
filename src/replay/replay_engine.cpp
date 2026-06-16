#include "datagine/replay/replay_engine.hpp"

#include "datagine/feed/csv_parser.hpp"

#include <iomanip>
#include <sstream>

namespace datagine {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void hash_byte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kFnvPrime;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (int shift = 0; shift < 64; shift += 8) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void hash_i64(std::uint64_t& hash, std::int64_t value) noexcept {
    hash_u64(hash, static_cast<std::uint64_t>(value));
}

void hash_price_level(std::uint64_t& hash, const PriceLevelSnapshot& level) noexcept {
    hash_i64(hash, level.price.ticks);
    hash_u64(hash, level.quantity.units);
    hash_u64(hash, level.orders.size());

    for (const auto& order : level.orders) {
        hash_u64(hash, order.order_id.value);
        hash_i64(hash, order.timestamp.nanoseconds);
        hash_u64(hash, order.quantity.units);
    }
}

void append_level(std::ostringstream& output, const std::optional<PriceLevelView>& level) {
    if (!level.has_value()) {
        output << "none";
        return;
    }

    output << level->price.ticks << 'x' << level->quantity.units;
}

}  // namespace

ReplayResult ReplayEngine::replay_file(
    const std::filesystem::path& input_path,
    ReplayOptions options) {
    book_.clear();

    ReplayResult result{};
    CsvParser parser{input_path};
    if (!parser.is_open()) {
        result.file_open_failed = true;
        result.errors.push_back(ReplayError{
            ReplayErrorType::FileOpenFailure,
            0,
            "failed to open input file",
            input_path.string(),
        });
        result.final_top = book_.top_of_book();
        result.final_snapshot = book_.snapshot();
        result.final_checksum = checksum_snapshot(result.final_snapshot);
        return result;
    }

    AnomalyMonitor anomaly_monitor{options.anomaly_config};

    for (;;) {
        const auto parsed = parser.next();
        if (parsed.status == CsvParseStatus::EndOfFile) {
            break;
        }

        if (parsed.status == CsvParseStatus::MalformedRow) {
            ++result.rejected_events;
            result.errors.push_back(ReplayError{
                ReplayErrorType::ParseError,
                parsed.line_number,
                parsed.message,
                parsed.raw_row,
            });
            continue;
        }

        const auto apply_result = book_.apply(*parsed.event);
        if (apply_result != BookResult::Applied) {
            ++result.rejected_events;
            result.errors.push_back(ReplayError{
                ReplayErrorType::ApplyRejected,
                parsed.line_number,
                std::string{"book rejected event: "} + std::string{to_string(apply_result)},
                parsed.raw_row,
            });
            continue;
        }

        ++result.events_processed;

        if (options.enable_anomaly_detection) {
            auto anomalies = anomaly_monitor.observe(*parsed.event, book_);
            for (auto& anomaly : anomalies) {
                ++result.anomaly_reason_counts[anomaly.reason];
                result.anomalies.push_back(std::move(anomaly));
            }
        }

        if (options.validate_after_apply && !book_.validate_invariants(options.invariant_options)) {
            result.invariant_failed = true;
            result.errors.push_back(ReplayError{
                ReplayErrorType::InvariantFailure,
                parsed.line_number,
                "book invariant validation failed",
                parsed.raw_row,
            });
            break;
        }
    }

    result.final_top = book_.top_of_book();
    result.final_snapshot = book_.snapshot();
    result.final_checksum = checksum_snapshot(result.final_snapshot);
    return result;
}

const LimitOrderBook& ReplayEngine::book() const noexcept {
    return book_;
}

std::uint64_t checksum_book(const LimitOrderBook& book) {
    return checksum_snapshot(book.snapshot());
}

std::uint64_t checksum_snapshot(const BookSnapshot& snapshot) {
    auto hash = kFnvOffsetBasis;

    hash_byte(hash, 'D');
    hash_byte(hash, 'G');
    hash_u64(hash, snapshot.bids.size());
    for (const auto& level : snapshot.bids) {
        hash_byte(hash, 'B');
        hash_price_level(hash, level);
    }

    hash_u64(hash, snapshot.asks.size());
    for (const auto& level : snapshot.asks) {
        hash_byte(hash, 'A');
        hash_price_level(hash, level);
    }

    return hash;
}

std::string format_checksum(std::uint64_t checksum) {
    std::ostringstream output{};
    output << "0x" << std::hex << std::setfill('0') << std::setw(16) << checksum;
    return output.str();
}

std::string format_top_of_book(const TopOfBook& top) {
    std::ostringstream output{};
    output << "bid=";
    append_level(output, top.bid);
    output << " ask=";
    append_level(output, top.ask);
    return output.str();
}

}  // namespace datagine
