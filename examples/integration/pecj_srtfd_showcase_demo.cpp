/**
 * @file pecj_srtfd_showcase_demo.cpp
 * @brief Combined PECJ out-of-order stream aggregation and SRTFD diagnosis demo.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "sage_tsdb/compute/srtfd_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

using namespace sage_tsdb;

namespace {

struct DemoOptions {
    size_t events_per_stream = 48;
    size_t windows = 6;
    int64_t window_us = 12000;
    int64_t slide_us = 6000;
    double srtfd_threshold = 3.0;
};

struct StreamEvent {
    std::string stream;
    uint64_t key = 0;
    double value = 0.0;
    int64_t event_time_us = 0;
    int64_t arrival_time_us = 0;
};

struct JoinAggregate {
    size_t s_count = 0;
    size_t r_count = 0;
    size_t matching_pairs = 0;
    double value_sum = 0.0;
};

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --events <n>       Events per stream (default: 48)\n"
              << "  --windows <n>      Windows to execute (default: 6)\n"
              << "  --window-us <n>    Window length in microseconds (default: 12000)\n"
              << "  --slide-us <n>     Window slide in microseconds (default: 6000)\n"
              << "  --threshold <v>    SRTFD anomaly threshold (default: 3.0)\n"
              << "  --help             Show this help\n";
}

bool parseOptions(int argc, char** argv, DemoOptions& options) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for option: " << arg << "\n";
            return false;
        }

        std::string value = argv[++i];
        if (arg == "--events") {
            options.events_per_stream = std::stoull(value);
        } else if (arg == "--windows") {
            options.windows = std::stoull(value);
        } else if (arg == "--window-us") {
            options.window_us = std::stoll(value);
        } else if (arg == "--slide-us") {
            options.slide_us = std::stoll(value);
        } else if (arg == "--threshold") {
            options.srtfd_threshold = std::stod(value);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }

    return options.events_per_stream > 0 && options.windows > 0
        && options.window_us > 0 && options.slide_us > 0
        && options.srtfd_threshold > 0.0;
}

std::vector<StreamEvent> generateOutOfOrderStreams(size_t events_per_stream) {
    std::vector<StreamEvent> events;
    events.reserve(events_per_stream * 2);
    constexpr int64_t base_ts = 1'000'000;
    constexpr int64_t step_us = 1'000;

    for (size_t i = 0; i < events_per_stream; ++i) {
        int64_t event_ts = base_ts + static_cast<int64_t>(i) * step_us;
        int64_t s_delay = static_cast<int64_t>((i % 5) * 700);
        int64_t r_delay = static_cast<int64_t>(((i + 2) % 7) * 550);
        if (i % 9 == 0) {
            s_delay += 5'000;
        }
        if (i % 11 == 0) {
            r_delay += 4'000;
        }

        uint64_t key = static_cast<uint64_t>((i * 7) % 13);
        events.push_back(StreamEvent{"S", key, 100.0 + static_cast<double>(i % 17), event_ts, event_ts + s_delay});
        events.push_back(StreamEvent{"R", key, 200.0 + static_cast<double>((i * 3) % 19), event_ts + 400, event_ts + r_delay});
    }

    std::sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.arrival_time_us != rhs.arrival_time_us) {
            return lhs.arrival_time_us < rhs.arrival_time_us;
        }
        return lhs.stream < rhs.stream;
    });
    return events;
}

size_t countOutOfOrderEvents(const std::vector<StreamEvent>& events, const std::string& stream) {
    int64_t max_event_time = 0;
    size_t out_of_order = 0;
    for (const auto& event : events) {
        if (event.stream != stream) {
            continue;
        }
        if (max_event_time != 0 && event.event_time_us < max_event_time) {
            ++out_of_order;
        }
        max_event_time = std::max(max_event_time, event.event_time_us);
    }
    return out_of_order;
}

void insertStreamEvents(TimeSeriesDB& db, const std::vector<StreamEvent>& events) {
    for (const auto& event : events) {
        TimeSeriesData data(event.event_time_us, event.value);
        data.tags["key"] = std::to_string(event.key);
        data.tags["stream"] = event.stream;
        data.fields["arrival_time_us"] = std::to_string(event.arrival_time_us);
        data.fields["value"] = std::to_string(event.value);
        db.insert(event.stream == "S" ? "stream_s" : "stream_r", data);
    }
}

JoinAggregate aggregateWindow(const TimeSeriesDB& db, int64_t start_us, int64_t end_us) {
    JoinAggregate aggregate;
    TimeRange query_range(start_us, end_us - 1);
    auto s_data = db.query("stream_s", query_range);
    auto r_data = db.query("stream_r", query_range);
    aggregate.s_count = s_data.size();
    aggregate.r_count = r_data.size();

    std::map<std::string, size_t> s_by_key;
    std::map<std::string, size_t> r_by_key;
    for (const auto& data : s_data) {
        auto key = data.tags.find("key");
        if (key != data.tags.end()) {
            ++s_by_key[key->second];
        }
        aggregate.value_sum += data.as_double();
    }
    for (const auto& data : r_data) {
        auto key = data.tags.find("key");
        if (key != data.tags.end()) {
            ++r_by_key[key->second];
        }
        aggregate.value_sum += data.as_double();
    }

    for (const auto& [key, s_count] : s_by_key) {
        auto r_it = r_by_key.find(key);
        if (r_it != r_by_key.end()) {
            aggregate.matching_pairs += s_count * r_it->second;
        }
    }
    return aggregate;
}

void printHeader(const std::string& title) {
    std::cout << "\n" << std::string(78, '=') << "\n"
              << title << "\n"
              << std::string(78, '=') << "\n";
}

bool runPecjSection(TimeSeriesDB& db, const DemoOptions& options) {
    printHeader("PECJ out-of-order stream window aggregation");

    auto events = generateOutOfOrderStreams(options.events_per_stream);
    insertStreamEvents(db, events);

    std::cout << "Inserted " << events.size() << " arrival-ordered events into stream_s/stream_r\n";
    std::cout << "Out-of-order arrivals: S=" << countOutOfOrderEvents(events, "S")
              << ", R=" << countOutOfOrderEvents(events, "R") << "\n";

#ifdef PECJ_MODE_INTEGRATED
    compute::ComputeConfig pecj_config;
    pecj_config.operator_type = "IMA";
    pecj_config.window_len_us = static_cast<uint64_t>(options.window_us);
    pecj_config.slide_len_us = static_cast<uint64_t>(options.slide_us);
    pecj_config.watermark_tag = "arrival";
    pecj_config.watermark_time_ms = 1000;
    pecj_config.stream_s_table = "stream_s";
    pecj_config.stream_r_table = "stream_r";
    pecj_config.result_table = "join_results";
    pecj_config.max_threads = 2;
    pecj_config.join_sum = false;

    compute::PECJComputeEngine pecj_engine;
    if (!pecj_engine.initialize(pecj_config, &db, nullptr)) {
        std::cerr << "Failed to initialize PECJ compute engine\n";
        return false;
    }

#ifdef PECJ_FULL_INTEGRATION
    std::cout << "PECJ mode: full integration\n";
#else
    std::cout << "PECJ mode: integrated API with stub backend\n";
#endif

    constexpr int64_t base_ts = 1'000'000;
        std::cout << "\nWindow   S    R    db_key_pairs   pecj_pairs   latency_ms   status\n";
    for (size_t window_id = 0; window_id < options.windows; ++window_id) {
        int64_t start_us = base_ts + static_cast<int64_t>(window_id) * options.slide_us;
        int64_t end_us = start_us + options.window_us;
        auto expected = aggregateWindow(db, start_us, end_us);
        auto status = pecj_engine.executeWindowJoin(
            static_cast<uint64_t>(window_id + 1),
            compute::TimeRange(start_us, end_us));
        std::string status_label = status.success
            ? (status.error.empty() ? "ok" : status.error)
            : status.error;

        std::cout << std::setw(6) << (window_id + 1)
                  << std::setw(5) << expected.s_count
                  << std::setw(5) << expected.r_count
                      << std::setw(15) << expected.matching_pairs
                  << std::setw(13) << status.join_count
                  << std::setw(13) << std::fixed << std::setprecision(3) << status.computation_time_ms
                  << "   " << status_label << "\n";
    }

    auto metrics = pecj_engine.getMetrics();
    std::cout << "\nPECJ windows completed: " << metrics.total_windows_completed
              << ", tuples seen: " << metrics.total_tuples_processed
              << ", avg latency: " << std::fixed << std::setprecision(3)
              << metrics.avg_window_latency_ms << " ms\n";
    return true;
#else
    std::cerr << "PECJ_MODE_INTEGRATED is not enabled for this build\n";
    return false;
#endif
}

std::vector<double> makeSensorVector(size_t index) {
    std::vector<double> features = {
        0.8 + static_cast<double>(index % 3) * 0.08,
        1.0 + static_cast<double>(index % 5) * 0.05,
        0.7 + static_cast<double>(index % 4) * 0.04,
        1.1 + static_cast<double>(index % 6) * 0.03,
        0.9 + static_cast<double>(index % 7) * 0.02,
        1.2 + static_cast<double>(index % 2) * 0.06
    };

    if (index == 13 || index == 24 || index == 31) {
        features = {4.9, 4.3, 5.2, 4.6, 5.0, 4.8};
    }
    return features;
}

void insertSensorEvents(TimeSeriesDB& db, size_t sample_count) {
    constexpr int64_t base_ts = 2'000'000;
    for (size_t i = 0; i < sample_count; ++i) {
        TimeSeriesData sample(base_ts + static_cast<int64_t>(i) * 1'000, makeSensorVector(i));
        sample.tags["asset_id"] = i < sample_count / 2 ? "roller-A" : "roller-B";
        sample.fields["source"] = "synthetic_cooling_roller";
        db.insert("sensor_events", sample);
    }
}

bool runSrtfdSection(TimeSeriesDB& db, const DemoOptions& options) {
    printHeader("SRTFD continuous fault diagnosis");

    insertSensorEvents(db, 40);

    compute::SRTFDConfig srtfd_config;
    srtfd_config.backend = "statistical";
    srtfd_config.dataset = "synthetic_cooling_roller";
    srtfd_config.input_table = "sensor_events";
    srtfd_config.result_table = "srtfd_results";
    srtfd_config.input_dim = 6;
    srtfd_config.num_classes = 5;
    srtfd_config.anomaly_threshold = options.srtfd_threshold;
    srtfd_config.strict_input_dim = true;
    srtfd_config.write_results = true;

    compute::SRTFDComputeEngine srtfd_engine;
    if (!srtfd_engine.initialize(srtfd_config, &db, nullptr)) {
        std::cerr << "Failed to initialize SRTFD compute engine\n";
        return false;
    }

    constexpr int64_t base_ts = 2'000'000;
    constexpr int64_t diagnosis_window_us = 10'000;
    constexpr int64_t diagnosis_slide_us = 8'000;

    std::cout << "Window   input   results   anomalies   max_score   avg_confidence\n";
    for (size_t window_id = 0; window_id < 5; ++window_id) {
        int64_t start_us = base_ts + static_cast<int64_t>(window_id) * diagnosis_slide_us;
        int64_t end_us = start_us + diagnosis_window_us;
        auto status = srtfd_engine.executeWindowDiagnosis(
            static_cast<uint64_t>(window_id + 1),
            TimeRange(start_us, end_us));

        if (!status.success) {
            std::cerr << "SRTFD window failed: " << status.error << "\n";
            return false;
        }

        std::cout << std::setw(6) << (window_id + 1)
                  << std::setw(8) << status.input_count
                  << std::setw(10) << status.result_count
                  << std::setw(12) << status.anomaly_count
                  << std::setw(12) << std::fixed << std::setprecision(3) << status.max_anomaly_score
                  << std::setw(17) << std::fixed << std::setprecision(3) << status.avg_confidence
                  << "\n";
    }

    auto metrics = srtfd_engine.getMetrics();
    std::cout << "\nSRTFD windows completed: " << metrics.total_windows_completed
              << ", samples processed: " << metrics.total_samples_processed
              << ", anomalies: " << metrics.total_anomalies
              << ", anomaly rate: " << std::fixed << std::setprecision(3)
              << metrics.anomaly_rate << "\n";

    auto results = db.query("srtfd_results", TimeRange(base_ts, base_ts + 50'000));
    std::cout << "\nAnomaly rows written to srtfd_results:\n";
    for (const auto& row : results) {
        auto is_anomaly = row.fields.find("is_anomaly");
        if (is_anomaly == row.fields.end() || is_anomaly->second != "true") {
            continue;
        }
        std::cout << "  t=" << row.timestamp
                  << " asset=" << row.tags.at("asset_id")
                  << " class=" << row.fields.at("fault_class")
                  << " score=" << row.fields.at("anomaly_score")
                  << " confidence=" << row.fields.at("confidence") << "\n";
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    DemoOptions options;
    if (!parseOptions(argc, argv, options)) {
        return argc > 1 && std::string(argv[1]) == "--help" ? 0 : 1;
    }

    std::cout << "sageTSDB PECJ + SRTFD showcase\n"
              << "Events per stream: " << options.events_per_stream
              << ", windows: " << options.windows
              << ", window_us: " << options.window_us
              << ", slide_us: " << options.slide_us << "\n";

    TimeSeriesDB db;
    db.createTable("stream_s", TableType::Stream);
    db.createTable("stream_r", TableType::Stream);
    db.createTable("join_results", TableType::JoinResult);
    db.createTable("sensor_events", TableType::Stream);

    if (!runPecjSection(db, options)) {
        return 1;
    }
    if (!runSrtfdSection(db, options)) {
        return 1;
    }

    std::cout << "\nDemo completed successfully.\n";
    return 0;
}