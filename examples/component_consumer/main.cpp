#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

#include <iostream>
#include <string>

namespace {

sage_tsdb::TimeSeriesData record(int64_t timestamp, uint64_t key, double value) {
    sage_tsdb::TimeSeriesData data;
    data.timestamp = timestamp;
    data.value = value;
    data.tags["key"] = std::to_string(key);
    data.fields["value"] = std::to_string(value);
    return data;
}

}  // namespace

int main() {
    sage_tsdb::TimeSeriesDB db;
    if (!db.createTable("stream_s", sage_tsdb::TableType::Stream) ||
        !db.createTable("stream_r", sage_tsdb::TableType::Stream)) {
        std::cerr << "failed to create input tables\n";
        return 1;
    }

    db.insert("stream_s", record(1'000'000, 7, 10.0));
    db.insert("stream_s", record(1'100'000, 8, 20.0));
    db.insert("stream_r", record(1'050'000, 7, 30.0));
    db.insert("stream_r", record(1'150'000, 9, 40.0));

    sage_tsdb::compute::ComputeConfig config;
    config.operator_type = "IAWJ";
    config.window_len_us = 500'000;
    config.slide_len_us = 500'000;
    config.watermark_time_ms = 0;

    sage_tsdb::compute::PECJComputeEngine engine;
    if (!engine.initialize(config, &db, nullptr)) {
        std::cerr << "failed to initialize PECJ engine\n";
        return 2;
    }

    const auto result = engine.executeWindowJoin(
        1, sage_tsdb::compute::TimeRange(1'000'000, 1'500'000));
    if (!result.success) {
        std::cerr << result.error << '\n';
        return 3;
    }

    std::cout << "join_count=" << result.join_count << '\n';
    return result.join_count == 1 ? 0 : 4;
}
