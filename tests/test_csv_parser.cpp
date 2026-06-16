#include "datagine/feed/csv_parser.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using namespace datagine;

std::filesystem::path write_fixture(std::string name, const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / std::move(name);
    std::ofstream output{path};
    output << content;
    return path;
}

void assert_event(
    const CsvParseResult& result,
    EventType type,
    Timestamp::rep timestamp,
    OrderId::rep order_id,
    Side side,
    Price::rep price,
    Quantity::rep quantity) {
    assert(result.status == CsvParseStatus::Event);
    assert(result.event.has_value());
    assert(result.event->type == type);
    assert(result.event->timestamp == Timestamp{timestamp});
    assert(result.event->order_id == OrderId{order_id});
    assert(result.event->side == side);
    assert(result.event->price == Price{price});
    assert(result.event->quantity == Quantity{quantity});
}

void parses_schema_example() {
    const auto path = write_fixture(
        "datagine_csv_parser_example.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "100000001,ADD,1,B,10025,200\n"
        "100000004,ADD,2,S,10030,100\n"
        "100000009,CANCEL,1,,,\n"
        "100000012,MODIFY,2,,10030,50\n"
        "100000020,EXECUTE,2,,10030,25\n");

    CsvParser parser{path};
    assert(parser.is_open());

    assert_event(parser.next(), EventType::Add, 100000001, 1, Side::Buy, 10025, 200);
    assert_event(parser.next(), EventType::Add, 100000004, 2, Side::Sell, 10030, 100);
    assert_event(parser.next(), EventType::Cancel, 100000009, 1, Side::Buy, 0, 0);
    assert_event(parser.next(), EventType::Modify, 100000012, 2, Side::Buy, 10030, 50);
    assert_event(parser.next(), EventType::Execute, 100000020, 2, Side::Buy, 10030, 25);
    assert(parser.next().status == CsvParseStatus::EndOfFile);
}

void malformed_rows_are_reported() {
    const auto bad_header = write_fixture(
        "datagine_bad_header.csv",
        "timestamp,event_type,order_id,side,price,quantity\n"
        "1,ADD,1,B,100,10\n");
    CsvParser bad_header_parser{bad_header};
    auto result = bad_header_parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.line_number == 1);
    assert(result.message == "invalid CSV header");

    const auto path = write_fixture(
        "datagine_malformed_rows.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "1,ADD,1,B,100\n"
        "bad,ADD,1,B,100,10\n"
        "1,UNKNOWN,1,B,100,10\n"
        "1,ADD,1,,100,10\n"
        "1,ADD,1,X,100,10\n"
        "1,CANCEL,1,,100,\n");

    CsvParser parser{path};

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "expected 6 columns");

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "invalid timestamp_ns");

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "unknown event_type");

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "ADD requires side B or S");

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "ADD requires side B or S");

    result = parser.next();
    assert(result.status == CsvParseStatus::MalformedRow);
    assert(result.message == "CANCEL requires empty side, price, and quantity");

    assert(parser.next().status == CsvParseStatus::EndOfFile);
}

}  // namespace

int main() {
    parses_schema_example();
    malformed_rows_are_reported();

    return 0;
}
