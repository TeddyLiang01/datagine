#include "datagine/replay/replay_engine.hpp"

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

void assert_level(
    const PriceLevelView& level,
    Price::rep price,
    Quantity::rep quantity,
    std::size_t order_count) {
    assert(level.price == Price{price});
    assert(level.quantity == Quantity{quantity});
    assert(level.order_count == order_count);
}

void successful_replay_is_deterministic() {
    const auto path = write_fixture(
        "datagine_replay_success.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "100000001,ADD,1,B,10025,200\n"
        "100000004,ADD,2,S,10030,100\n"
        "100000009,CANCEL,1,,,\n"
        "100000012,MODIFY,2,,10030,50\n"
        "100000020,EXECUTE,2,,10030,25\n"
        "100000025,ADD,3,B,10020,10\n");

    ReplayEngine first_engine;
    ReplayEngine second_engine;

    const auto first = first_engine.replay_file(path);
    const auto second = second_engine.replay_file(path);

    assert(first.events_processed == 6);
    assert(first.rejected_events == 0);
    assert(!first.file_open_failed);
    assert(!first.invariant_failed);

    assert(first.final_top.ask.has_value());
    assert(first.final_top.bid.has_value());
    assert_level(*first.final_top.ask, 10030, 25, 1);
    assert_level(*first.final_top.bid, 10020, 10, 1);

    assert(first.final_snapshot == second.final_snapshot);
    assert(first.final_checksum == second.final_checksum);
    assert(format_checksum(first.final_checksum).size() == 18);
    assert(format_top_of_book(first.final_top) == "bid=10020x10 ask=10030x25");
}

void malformed_rows_are_rejected_and_replay_continues() {
    const auto path = write_fixture(
        "datagine_replay_malformed.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "1,ADD,1,B,100,10\n"
        "2,ADD,2,X,101,10\n"
        "3,ADD,3,S,102,4\n");

    ReplayEngine engine;
    const auto result = engine.replay_file(path);

    assert(result.events_processed == 2);
    assert(result.rejected_events == 1);
    assert(result.errors.size() == 1);
    assert(result.errors[0].type == ReplayErrorType::ParseError);
    assert(result.final_top.bid.has_value());
    assert(result.final_top.ask.has_value());
    assert_level(*result.final_top.bid, 100, 10, 1);
    assert_level(*result.final_top.ask, 102, 4, 1);
}

void book_rejections_are_rejected_events() {
    const auto path = write_fixture(
        "datagine_replay_book_rejection.csv",
        "timestamp_ns,event_type,order_id,side,price,quantity\n"
        "1,ADD,1,B,100,10\n"
        "2,CANCEL,999,,,\n"
        "3,EXECUTE,1,,100,4\n");

    ReplayEngine engine;
    const auto result = engine.replay_file(path);

    assert(result.events_processed == 2);
    assert(result.rejected_events == 1);
    assert(result.errors.size() == 1);
    assert(result.errors[0].type == ReplayErrorType::ApplyRejected);
    assert(result.final_top.bid.has_value());
    assert_level(*result.final_top.bid, 100, 6, 1);
}

}  // namespace

int main() {
    successful_replay_is_deterministic();
    malformed_rows_are_rejected_and_replay_continues();
    book_rejections_are_rejected_events();

    return 0;
}
