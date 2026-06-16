#include "datagine/replay/replay_engine.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: datagine_replay path/to/events.csv [--anomaly-report]\n";
        return 2;
    }

    datagine::ReplayOptions options{};
    if (argc == 3) {
        if (std::string_view{argv[2]} != "--anomaly-report") {
            std::cerr << "usage: datagine_replay path/to/events.csv [--anomaly-report]\n";
            return 2;
        }

        options.enable_anomaly_detection = true;
    }

    datagine::ReplayEngine engine;
    const auto result = engine.replay_file(std::filesystem::path{argv[1]}, options);

    std::cout << "events_processed: " << result.events_processed << '\n';
    std::cout << "rejected_events: " << result.rejected_events << '\n';
    std::cout << "final_top_of_book: " << datagine::format_top_of_book(result.final_top) << '\n';
    std::cout << "final_checksum: " << datagine::format_checksum(result.final_checksum) << '\n';

    if (options.enable_anomaly_detection) {
        std::vector<std::pair<std::string, std::size_t>> reasons{
            result.anomaly_reason_counts.begin(),
            result.anomaly_reason_counts.end(),
        };
        std::sort(reasons.begin(), reasons.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second != rhs.second) {
                return lhs.second > rhs.second;
            }
            return lhs.first < rhs.first;
        });

        std::cout << "anomaly_count: " << result.anomalies.size() << '\n';
        std::cout << "top_anomaly_reasons: ";
        if (reasons.empty()) {
            std::cout << "none";
        } else {
            const auto limit = std::min<std::size_t>(5, reasons.size());
            for (std::size_t i = 0; i < limit; ++i) {
                if (i != 0) {
                    std::cout << ", ";
                }
                std::cout << reasons[i].first << '=' << reasons[i].second;
            }
        }
        std::cout << '\n';
    }

    for (const auto& error : result.errors) {
        std::cerr << "line " << error.line_number << ": " << error.message << '\n';
    }

    if (result.file_open_failed || result.invariant_failed) {
        return 1;
    }

    return 0;
}
