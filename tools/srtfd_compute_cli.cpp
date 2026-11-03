#include "sage_tsdb/compute/srtfd_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::string> split(const std::string& line) {
    std::istringstream input(line);
    std::string field;
    std::vector<std::string> result;
    while (std::getline(input, field, ',')) result.push_back(field);
    return result;
}

size_t column(const std::vector<std::string>& header, const std::string& name) {
    for (size_t i = 0; i < header.size(); ++i) if (header[i] == name) return i;
    throw std::invalid_argument("Missing CSV column: " + name);
}

int run(int argc, char** argv) {
    std::string model, input_file, output_file;
    size_t input_dim = 52, classes = 22;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--help") {
            std::cout << "sage_tsdb_srtfd --model model.ts --input sensors.csv "
                         "[--output predictions.csv] [--dim 52] [--classes 22]\n"
                         "CSV: f0,...,fN; optional timestamp_us and expected_class.\n";
            return 0;
        }
        if (++i >= argc) throw std::invalid_argument("Missing value: " + option);
        const std::string value = argv[i];
        if (option == "--model") model = value;
        else if (option == "--input") input_file = value;
        else if (option == "--output") output_file = value;
        else if (option == "--dim" || option == "--classes") {
            size_t parsed = 0;
            const auto number = std::stoull(value, &parsed);
            if (parsed != value.size() || !number || number > 4096 || value[0] == '-')
                throw std::invalid_argument("Invalid dimension: " + value);
            if (option == "--dim") input_dim = number;
            else classes = number;
        } else throw std::invalid_argument("Unknown option: " + option);
    }
    if (model.empty() || input_file.empty())
        throw std::invalid_argument("--model and --input are required");
    std::ifstream input(input_file);
    if (!input) throw std::runtime_error("Cannot open sensor CSV");
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("Empty sensor CSV");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = split(line);
    std::vector<size_t> features;
    for (size_t i = 0; i < input_dim; ++i) features.push_back(column(header, "f" + std::to_string(i)));
    size_t timestamp_column = header.size(), expected_column = header.size();
    for (size_t i = 0; i < header.size(); ++i) {
        if (header[i] == "timestamp_us") timestamp_column = i;
        if (header[i] == "expected_class") expected_column = i;
    }
    sage_tsdb::TimeSeriesDB db;
    db.createTable("sensor_events", sage_tsdb::TableType::Stream);
    std::vector<int> expected;
    int64_t min_timestamp = std::numeric_limits<int64_t>::max();
    int64_t max_timestamp = std::numeric_limits<int64_t>::min();
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto row = split(line);
        if (row.size() != header.size()) throw std::invalid_argument("Inconsistent CSV row width");
        std::vector<double> values;
        for (auto index : features) {
            size_t parsed = 0;
            const auto value = std::stod(row[index], &parsed);
            if (parsed != row[index].size() || !std::isfinite(value))
                throw std::invalid_argument("Invalid sensor value");
            values.push_back(value);
        }
        const int64_t timestamp = timestamp_column < row.size()
            ? std::stoll(row[timestamp_column]) : static_cast<int64_t>(expected.size() + 1);
        if (timestamp <= max_timestamp) throw std::invalid_argument("Sensor CSV timestamps must increase");
        min_timestamp = std::min(min_timestamp, timestamp);
        max_timestamp = timestamp;
        sage_tsdb::TimeSeriesData sample(timestamp, values);
        sample.tags["asset_id"] = "neural-cli";
        db.insert("sensor_events", sample);
        expected.push_back(expected_column < row.size() ? std::stoi(row[expected_column]) : -1);
    }
    if (expected.empty()) throw std::invalid_argument("No sensor rows");
    sage_tsdb::compute::SRTFDConfig config;
    config.backend = "torchscript";
    config.model_path = model;
    config.input_dim = input_dim;
    config.num_classes = classes;
    sage_tsdb::compute::SRTFDComputeEngine engine;
    if (!engine.initialize(config, &db, nullptr))
        throw std::runtime_error("Cannot load classifier; check model shape and LibTorch/export version");
    const auto status = engine.executeWindowDiagnosis(1, sage_tsdb::TimeRange(min_timestamp, max_timestamp));
    if (!status.success || status.result_count != expected.size())
        throw std::runtime_error("Diagnosis failed: " + status.error);
    const auto results = db.query("srtfd_results", sage_tsdb::TimeRange(min_timestamp, max_timestamp));
    std::ofstream output;
    if (!output_file.empty()) {
        output.open(output_file);
        if (!output) throw std::runtime_error("Cannot open output CSV");
        output << "timestamp_us,predicted_class,confidence,anomaly_score\n";
    }
    size_t checked = 0, mismatches = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        const int prediction = std::stoi(results[i].fields.at("fault_class"));
        if (expected[i] >= 0) {
            ++checked;
            mismatches += prediction != expected[i];
        }
        if (output) output << results[i].timestamp << ',' << prediction << ','
                           << results[i].fields.at("confidence") << ','
                           << results[i].fields.at("anomaly_score") << '\n';
    }
    std::cout << "{\"success\":" << (mismatches ? "false" : "true")
              << ",\"backend\":\"torchscript\",\"samples\":" << status.result_count
              << ",\"anomalies\":" << status.anomaly_count
              << ",\"checked\":" << checked << ",\"mismatches\":" << mismatches << "}\n";
    return mismatches ? 2 : 0;
}
} // namespace

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "sage_tsdb_srtfd: " << error.what() << '\n';
        return 1;
    }
}
