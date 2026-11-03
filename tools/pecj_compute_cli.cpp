#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using sage_tsdb::TableType;
using sage_tsdb::TimeSeriesData;
using sage_tsdb::TimeSeriesDB;
using sage_tsdb::compute::ComputeConfig;
using sage_tsdb::compute::ComputeStatus;
using sage_tsdb::compute::PECJComputeEngine;

struct Options {
    std::string operator_type = "IAWJ";
    std::string s_file;
    std::string r_file;
    std::string model_root;
    std::string model_prefix = "linearVAE";
    uint64_t watermark_ms = 0;
    int64_t window_start_us = 0;
    int64_t window_end_us = 0;
    uint64_t window_id = 1;
    bool self_test = false;
};

struct InputData {
    std::vector<TimeSeriesData> stream_s;
    std::vector<TimeSeriesData> stream_r;
    int64_t min_timestamp = std::numeric_limits<int64_t>::max();
    int64_t max_timestamp = std::numeric_limits<int64_t>::min();
};

void printUsage(const char* program) {
    std::cout
        << "Usage:\n"
        << "  " << program << " --self-test\n"
        << "  " << program << " --s-file S.csv --r-file R.csv [options]\n\n"
        << "CSV schema: timestamp_us,key,value (one header row is optional)\n\n"
        << "Options:\n"
        << "  --operator NAME          IAWJ, IMA, PECJ, MeanAQP, MSWJ, IAWJSel,\n"
        << "                           LazyIAWJSel, AI, LinearSVI, SHJ, or PRJ\n"
        << "  --window-start-us N      Inclusive window start; defaults to min input time\n"
        << "  --window-end-us N        Exclusive window end; defaults to max input time + 1\n"
        << "  --window-id N            Window identifier (default: 1)\n"
        << "  --model-root DIR         Root containing original PECJ torchscripts/\n"
        << "  --model-prefix NAME      Upstream model set (default: linearVAE)\n"
        << "  --watermark-ms N         Arrival cutoff for upstream neural inference (default: 0=batch)\n"
        << "  --self-test              Run the deterministic built-in join test\n"
        << "  --version                Print component version\n"
        << "  --help                   Print this help\n";
}

template <typename T>
T parseInteger(const std::string& text, const std::string& option) {
    size_t parsed = 0;
    try {
        if constexpr (std::is_same_v<T, int64_t>) {
            const auto value = std::stoll(text, &parsed);
            if (parsed != text.size()) {
                throw std::invalid_argument("trailing characters");
            }
            return static_cast<T>(value);
        } else {
            const auto value = std::stoull(text, &parsed);
            if (parsed != text.size()) {
                throw std::invalid_argument("trailing characters");
            }
            return static_cast<T>(value);
        }
    } catch (const std::exception&) {
        throw std::invalid_argument("invalid integer for " + option + ": " + text);
    }
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const std::string& option) -> std::string {
            if (++i >= argc) {
                throw std::invalid_argument("missing value for " + option);
            }
            return argv[i];
        };

        if (arg == "--self-test") {
            options.self_test = true;
        } else if (arg == "--operator") {
            options.operator_type = requireValue(arg);
        } else if (arg == "--s-file") {
            options.s_file = requireValue(arg);
        } else if (arg == "--r-file") {
            options.r_file = requireValue(arg);
        } else if (arg == "--model-root") {
            options.model_root = requireValue(arg);
        } else if (arg == "--model-prefix") {
            options.model_prefix = requireValue(arg);
        } else if (arg == "--watermark-ms") {
            options.watermark_ms = parseInteger<uint64_t>(requireValue(arg), arg);
        } else if (arg == "--window-start-us") {
            options.window_start_us = parseInteger<int64_t>(requireValue(arg), arg);
        } else if (arg == "--window-end-us") {
            options.window_end_us = parseInteger<int64_t>(requireValue(arg), arg);
        } else if (arg == "--window-id") {
            options.window_id = parseInteger<uint64_t>(requireValue(arg), arg);
        } else if (arg == "--version") {
            std::cout << "sageTSDB PECJ compute component "
                      << SAGE_TSDB_COMPONENT_VERSION << '\n';
            std::exit(0);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (!options.self_test && options.s_file.empty() && options.r_file.empty()) {
        options.self_test = true;
    }
    if (!options.self_test && (options.s_file.empty() || options.r_file.empty())) {
        throw std::invalid_argument("--s-file and --r-file must be provided together");
    }
    return options;
}

TimeSeriesData makeRecord(int64_t timestamp, uint64_t key, double value) {
    TimeSeriesData record;
    record.timestamp = timestamp;
    record.value = value;
    record.tags["key"] = std::to_string(key);
    record.fields["value"] = std::to_string(value);
    return record;
}

std::vector<TimeSeriesData> loadCsv(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open CSV file: " + path);
    }

    std::vector<TimeSeriesData> records;
    std::string line;
    size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream row(line);
        std::string timestamp_text;
        std::string key_text;
        std::string value_text;
        if (!std::getline(row, timestamp_text, ',') ||
            !std::getline(row, key_text, ',') ||
            !std::getline(row, value_text, ',')) {
            throw std::runtime_error(path + ":" + std::to_string(line_number) +
                                     ": expected timestamp_us,key,value");
        }

        if (line_number == 1 && timestamp_text == "timestamp_us") {
            continue;
        }

        try {
            size_t value_parsed = 0;
            const auto timestamp = parseInteger<int64_t>(timestamp_text, "timestamp_us");
            const auto key = parseInteger<uint64_t>(key_text, "key");
            const double value = std::stod(value_text, &value_parsed);
            if (value_parsed != value_text.size()) {
                throw std::invalid_argument("trailing characters");
            }
            auto record = makeRecord(timestamp, key, value);
            std::string arrival_text;
            if (std::getline(row, arrival_text)) {
                const auto arrival = parseInteger<int64_t>(arrival_text, "arrival_time_us");
                if (arrival < timestamp) throw std::invalid_argument("arrival precedes event time");
                record.fields["arrival_time_us"] = std::to_string(arrival);
            }
            records.push_back(std::move(record));
        } catch (const std::exception& error) {
            throw std::runtime_error(path + ":" + std::to_string(line_number) +
                                     ": " + error.what());
        }
    }

    if (records.empty()) {
        throw std::runtime_error("CSV file contains no records: " + path);
    }
    return records;
}

InputData loadInput(const Options& options) {
    InputData input;
    if (options.self_test) {
        input.stream_s = {
            makeRecord(1'000'000, 1, 10.0),
            makeRecord(1'100'000, 2, 20.0),
            makeRecord(1'200'000, 1, 30.0),
        };
        input.stream_r = {
            makeRecord(1'050'000, 1, 40.0),
            makeRecord(1'150'000, 3, 50.0),
            makeRecord(1'250'000, 1, 60.0),
        };
    } else {
        input.stream_s = loadCsv(options.s_file);
        input.stream_r = loadCsv(options.r_file);
    }

    for (const auto& record : input.stream_s) {
        input.min_timestamp = std::min(input.min_timestamp, record.timestamp);
        input.max_timestamp = std::max(input.max_timestamp, record.timestamp);
    }
    for (const auto& record : input.stream_r) {
        input.min_timestamp = std::min(input.min_timestamp, record.timestamp);
        input.max_timestamp = std::max(input.max_timestamp, record.timestamp);
    }
    return input;
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped.push_back(hex[(static_cast<unsigned char>(ch) >> 4) & 0x0f]);
                    escaped.push_back(hex[static_cast<unsigned char>(ch) & 0x0f]);
                } else {
                    escaped.push_back(ch);
                }
        }
    }
    return escaped;
}

void printStatus(const Options& options, const ComputeStatus& status) {
    std::cout << "{\n"
              << "  \"success\": " << (status.success ? "true" : "false") << ",\n"
              << "  \"operator\": \"" << jsonEscape(options.operator_type) << "\",\n"
              << "  \"window_id\": " << status.window_id << ",\n"
              << "  \"input_s_count\": " << status.input_s_count << ",\n"
              << "  \"input_r_count\": " << status.input_r_count << ",\n"
              << "  \"join_count\": " << status.join_count << ",\n"
              << "  \"aqp_estimate\": " << status.aqp_estimate << ",\n"
              << "  \"computation_time_ms\": " << status.computation_time_ms << ",\n"
              << "  \"error\": \"" << jsonEscape(status.error) << "\"\n"
              << "}\n";
}

int run(const Options& options) {
    auto input = loadInput(options);
    // Upstream PECJ resolves torchscripts/ relative to the process directory.
    // CSV input is loaded first so changing this CLI's cwd cannot change input resolution.
    if (!options.model_root.empty()) {
        std::filesystem::current_path(options.model_root);
    }
    if (options.operator_type == "AI" || options.operator_type == "LinearSVI") {
        for (const auto& kind : {"selectivity", "sRate", "rRate"}) {
            const auto file = std::filesystem::path("torchscripts") / options.model_prefix
                / (options.model_prefix + "_" + kind + ".pt");
            if (!std::filesystem::is_regular_file(file)) {
                throw std::invalid_argument("Missing upstream PECJ model: " + file.string());
            }
        }
    }
    const int64_t start_us = options.window_start_us == 0
                                 ? input.min_timestamp
                                 : options.window_start_us;
    const int64_t end_us = options.window_end_us == 0
                               ? (input.max_timestamp == std::numeric_limits<int64_t>::max()
                                      ? throw std::invalid_argument(
                                            "--window-end-us is required when input reaches INT64_MAX")
                                      : input.max_timestamp + 1)
                               : options.window_end_us;
    if (end_us <= start_us) {
        throw std::invalid_argument("window end must be greater than window start");
    }

    TimeSeriesDB db;
    if (!db.createTable("stream_s", TableType::Stream) ||
        !db.createTable("stream_r", TableType::Stream)) {
        throw std::runtime_error("failed to create in-memory stream tables");
    }
    db.insertBatch("stream_s", input.stream_s);
    db.insertBatch("stream_r", input.stream_r);

    ComputeConfig config;
    config.operator_type = options.operator_type;
    config.operator_enum = sage_tsdb::compute::stringToOperatorType(options.operator_type);
    config.window_len_us = static_cast<uint64_t>(end_us - start_us);
    config.slide_len_us = config.window_len_us;
    // The CLI supplies a complete window as a batch, so finalize at stop()
    // instead of simulating an early arrival-time watermark.
    config.watermark_time_ms = options.watermark_ms;
    config.ai_mode = "inference";
    config.model_prefix = options.model_prefix;

    PECJComputeEngine engine;
    if (!engine.initialize(config, &db, nullptr)) {
        throw std::runtime_error("failed to initialize PECJ compute engine");
    }

    const auto status = engine.executeWindowJoin(
        options.window_id,
        sage_tsdb::compute::TimeRange(start_us, end_us));
    printStatus(options, status);

    if (!status.success) {
        return 1;
    }
    if (options.self_test &&
        (status.input_s_count != 3 || status.input_r_count != 3 ||
         status.join_count != 4)) {
        std::cerr << "self-test failed: expected input counts 3/3 and join_count 4\n";
        return 2;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parseOptions(argc, argv));
    } catch (const std::exception& error) {
        std::cerr << "sage_tsdb_pecj: " << error.what() << '\n';
        return 64;
    }
}
