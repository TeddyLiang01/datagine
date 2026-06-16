#include "datagine/feed/csv_parser.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace datagine {

namespace {

constexpr std::string_view kExpectedHeader =
    "timestamp_ns,event_type,order_id,side,price,quantity";

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }

    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }

    return value;
}

std::vector<std::string_view> split_csv_columns(std::string_view row) {
    std::vector<std::string_view> columns{};
    std::size_t start = 0;

    while (start <= row.size()) {
        const auto comma = row.find(',', start);
        if (comma == std::string_view::npos) {
            columns.push_back(trim(row.substr(start)));
            break;
        }

        columns.push_back(trim(row.substr(start, comma - start)));
        start = comma + 1;
    }

    return columns;
}

template <typename T>
bool parse_integral(std::string_view text, T& value) {
    if (text.empty()) {
        return false;
    }

    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    return ec == std::errc{} && ptr == last;
}

std::optional<EventType> parse_event_type(std::string_view value) {
    if (value == "ADD") {
        return EventType::Add;
    }

    if (value == "CANCEL") {
        return EventType::Cancel;
    }

    if (value == "MODIFY") {
        return EventType::Modify;
    }

    if (value == "EXECUTE") {
        return EventType::Execute;
    }

    return std::nullopt;
}

std::optional<Side> parse_side(std::string_view value) {
    if (value == "B") {
        return Side::Buy;
    }

    if (value == "S") {
        return Side::Sell;
    }

    return std::nullopt;
}

bool all_empty(std::initializer_list<std::string_view> values) {
    for (const auto value : values) {
        if (!value.empty()) {
            return false;
        }
    }

    return true;
}

}  // namespace

CsvParser::CsvParser(std::filesystem::path input_path)
    : input_path_(std::move(input_path)),
      input_(input_path_) {}

const std::filesystem::path& CsvParser::input_path() const noexcept {
    return input_path_;
}

bool CsvParser::is_open() const noexcept {
    return input_.is_open();
}

CsvParseResult CsvParser::next() {
    if (!input_.is_open()) {
        return malformed(0, "input file is not open", {});
    }

    if (!header_checked_) {
        header_checked_ = true;

        std::string header{};
        if (!std::getline(input_, header)) {
            return CsvParseResult{CsvParseStatus::EndOfFile};
        }

        ++line_number_;
        header_valid_ = trim(header) == kExpectedHeader;
        if (!header_valid_) {
            return malformed(line_number_, "invalid CSV header", std::move(header));
        }
    }

    if (!header_valid_) {
        return CsvParseResult{CsvParseStatus::EndOfFile};
    }

    std::string row{};
    if (!std::getline(input_, row)) {
        return CsvParseResult{CsvParseStatus::EndOfFile};
    }

    ++line_number_;
    return parse_row(std::move(row));
}

void CsvParser::reset() {
    input_.close();
    input_.open(input_path_);
    line_number_ = 0;
    header_checked_ = false;
    header_valid_ = false;
}

CsvParseResult CsvParser::parse_row(std::string row) const {
    const auto columns = split_csv_columns(row);
    if (columns.size() != 6) {
        return malformed(line_number_, "expected 6 columns", std::move(row));
    }

    Timestamp::rep timestamp = 0;
    if (!parse_integral(columns[0], timestamp)) {
        return malformed(line_number_, "invalid timestamp_ns", std::move(row));
    }

    const auto event_type = parse_event_type(columns[1]);
    if (!event_type.has_value()) {
        return malformed(line_number_, "unknown event_type", std::move(row));
    }

    OrderId::rep order_id = 0;
    if (!parse_integral(columns[2], order_id)) {
        return malformed(line_number_, "invalid order_id", std::move(row));
    }

    Price::rep price = 0;
    Quantity::rep quantity = 0;
    Side side = Side::Buy;

    switch (*event_type) {
        case EventType::Add: {
            const auto parsed_side = parse_side(columns[3]);
            if (!parsed_side.has_value()) {
                return malformed(line_number_, "ADD requires side B or S", std::move(row));
            }

            if (!parse_integral(columns[4], price)) {
                return malformed(line_number_, "ADD requires numeric price", std::move(row));
            }

            if (!parse_integral(columns[5], quantity)) {
                return malformed(line_number_, "ADD requires numeric quantity", std::move(row));
            }

            side = *parsed_side;
            break;
        }
        case EventType::Cancel:
            if (!all_empty({columns[3], columns[4], columns[5]})) {
                return malformed(line_number_, "CANCEL requires empty side, price, and quantity", std::move(row));
            }
            break;
        case EventType::Modify:
            if (!columns[3].empty()) {
                return malformed(line_number_, "MODIFY requires empty side", std::move(row));
            }

            if (!parse_integral(columns[4], price)) {
                return malformed(line_number_, "MODIFY requires numeric price", std::move(row));
            }

            if (!parse_integral(columns[5], quantity)) {
                return malformed(line_number_, "MODIFY requires numeric quantity", std::move(row));
            }
            break;
        case EventType::Execute:
            if (!columns[3].empty()) {
                return malformed(line_number_, "EXECUTE requires empty side", std::move(row));
            }

            if (!parse_integral(columns[4], price)) {
                return malformed(line_number_, "EXECUTE requires numeric price", std::move(row));
            }

            if (!parse_integral(columns[5], quantity)) {
                return malformed(line_number_, "EXECUTE requires numeric quantity", std::move(row));
            }
            break;
    }

    return CsvParseResult{
        CsvParseStatus::Event,
        MarketEvent{
            *event_type,
            Timestamp{timestamp},
            OrderId{order_id},
            side,
            Price{price},
            Quantity{quantity},
        },
        line_number_,
        {},
        std::move(row),
    };
}

CsvParseResult CsvParser::malformed(
    std::size_t line_number,
    std::string message,
    std::string raw_row) const {
    return CsvParseResult{
        CsvParseStatus::MalformedRow,
        std::nullopt,
        line_number,
        std::move(message),
        std::move(raw_row),
    };
}

}  // namespace datagine
