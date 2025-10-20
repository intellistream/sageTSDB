#include <gtest/gtest.h>
#include "sage_tsdb/algorithms/window_aggregator.h"
#include <cmath>

using namespace sage_tsdb;

class WindowAggregatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_time = 1000000;
    }

    int64_t base_time;

    std::vector<TimeSeriesData> create_data(int count, int64_t start_time, int64_t interval = 1000) {
        std::vector<TimeSeriesData> data;
        for (int i = 0; i < count; ++i) {
            data.push_back(TimeSeriesData(start_time + i * interval, static_cast<double>(i + 1)));
        }
        return data;
    }
};

TEST_F(WindowAggregatorTest, SumAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "sum";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // First window should sum 1+2+...+10 = 55
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 55.0);
}

TEST_F(WindowAggregatorTest, AverageAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "avg";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // First window average: (1+2+...+10)/10 = 5.5
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 5.5);
}

TEST_F(WindowAggregatorTest, MinAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "min";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // First window min should be 1
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 1.0);
}

TEST_F(WindowAggregatorTest, MaxAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "max";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // First window max should be 10
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 10.0);
}

TEST_F(WindowAggregatorTest, CountAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "count";
    WindowAggregator agg(config);
    
    auto data = create_data(25, base_time);
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // First window should have 10 points
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 10.0);
}

TEST_F(WindowAggregatorTest, StdDevAggregation) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "10000";
    config["aggregation"] = "stddev";
    WindowAggregator agg(config);
    
    // Create data with known stddev
    std::vector<TimeSeriesData> data;
    for (int i = 0; i < 10; ++i) {
        data.push_back(TimeSeriesData(base_time + i * 1000, 5.0)); // All same value
    }
    
    auto results = agg.process(data);
    
    EXPECT_GT(results.size(), 0);
    // StdDev of all same values should be 0
    EXPECT_NEAR(std::get<double>(results[0].value), 0.0, 1e-6);
}

TEST_F(WindowAggregatorTest, TumblingWindows) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "5000";
    config["aggregation"] = "count";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    // Should have 4 windows (20 points / 5 points per window)
    EXPECT_EQ(results.size(), 4);
    
    // Each window should have 5 points
    for (const auto& result : results) {
        EXPECT_DOUBLE_EQ(std::get<double>(result.value), 5.0);
    }
}

TEST_F(WindowAggregatorTest, SlidingWindows) {
    AlgorithmConfig config;
    config["window_type"] = "sliding";
    config["window_size"] = "5000";
    config["slide_interval"] = "2000";
    config["aggregation"] = "count";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    auto results = agg.process(data);
    
    // Sliding windows should produce more results than tumbling
    EXPECT_GT(results.size(), 4);
}

TEST_F(WindowAggregatorTest, EmptyData) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "5000";
    config["aggregation"] = "avg";
    WindowAggregator agg(config);
    
    std::vector<TimeSeriesData> data;
    auto results = agg.process(data);
    
    EXPECT_EQ(results.size(), 0);
}

TEST_F(WindowAggregatorTest, SingleDataPoint) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "5000";
    config["aggregation"] = "avg";
    WindowAggregator agg(config);
    
    std::vector<TimeSeriesData> data = {TimeSeriesData(base_time, 42.0)};
    auto results = agg.process(data);
    
    EXPECT_EQ(results.size(), 1);
    EXPECT_DOUBLE_EQ(std::get<double>(results[0].value), 42.0);
}

TEST_F(WindowAggregatorTest, Statistics) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "5000";
    config["aggregation"] = "avg";
    WindowAggregator agg(config);
    
    auto data = create_data(20, base_time);
    agg.process(data);
    
    auto stats = agg.get_stats();
    
    EXPECT_GT(stats.at("windows_completed"), 0);
    EXPECT_GT(stats.at("data_points_processed"), 0);
}

TEST_F(WindowAggregatorTest, MultipleProcessCalls) {
    AlgorithmConfig config;
    config["window_type"] = "tumbling";
    config["window_size"] = "5000";
    config["aggregation"] = "sum";
    WindowAggregator agg(config);
    
    // Process in batches
    auto batch1 = create_data(10, base_time);
    auto results1 = agg.process(batch1);
    
    auto batch2 = create_data(10, base_time + 10000);
    auto results2 = agg.process(batch2);
    
    EXPECT_GT(results1.size(), 0);
    EXPECT_GT(results2.size(), 0);
}

