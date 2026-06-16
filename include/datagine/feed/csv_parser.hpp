#pragma once

#include "datagine/core/market_event.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace datagine {

enum class CsvParseStatus : std::uint8_t {
    Event,
    MalformedRow,
    EndOfFile,
};

struct CsvParseResult {
    CsvParseStatus status{CsvParseStatus::EndOfFile};
    std::optional<MarketEvent> event{};
    std::size_t line_number{};
    std::string message{};
    std::string raw_row{};
};

class CsvParser {
public:
    explicit CsvParser(std::filesystem::path input_path);

    [[nodiscard]] const std::filesystem::path& input_path() const noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] CsvParseResult next();

    void reset();

private:
    [[nodiscard]] CsvParseResult parse_row(std::string row) const;
    [[nodiscard]] CsvParseResult malformed(
        std::size_t line_number,
        std::string message,
        std::string raw_row) const;

    std::filesystem::path input_path_;
    std::ifstream input_;
    std::size_t line_number_{};
    bool header_checked_{false};
    bool header_valid_{false};
};

}  // namespace datagine
