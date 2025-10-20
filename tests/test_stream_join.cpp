#include <gtest/gtest.h>
#include "sage_tsdb/algorithms/stream_join.h"

using namespace sage_tsdb;

class StreamJoinTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_time = 1000000;
    }

    int64_t base_time;

    std::vector<TimeSeriesData> create_stream(int count, int64_t start_time, int64_t interval = 1000) {
        std::vector<TimeSeriesData> stream;
        for (int i = 0; i < count; ++i) {
            stream.push_back(TimeSeriesData(start_time + i * interval, static_cast<double>(i)));
        }
        return stream;
    }
};

TEST_F(StreamJoinTest, BasicJoin) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time);
    
    auto results = join.process_join(left, right);
    
    EXPECT_GT(results.size(), 0);
}

TEST_F(StreamJoinTest, JoinWithTimeOffset) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time + 500); // 500ms offset
    
    auto results = join.process_join(left, right);
    
    // Should find joins despite offset
    EXPECT_GT(results.size(), 0);
    
    // Verify time differences are within window
    for (const auto& [l, r] : results) {
        EXPECT_LE(std::abs(l.timestamp - r.timestamp), 5000);
    }
}

TEST_F(StreamJoinTest, OutOfOrderData) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "3000";
    StreamJoin join(config);
    
    // Create out-of-order left stream
    std::vector<TimeSeriesData> left = {
        TimeSeriesData(base_time + 5000, 5.0),
        TimeSeriesData(base_time + 2000, 2.0),
        TimeSeriesData(base_time + 7000, 7.0),
        TimeSeriesData(base_time + 1000, 1.0),
        TimeSeriesData(base_time + 4000, 4.0)
    };
    
    auto right = create_stream(10, base_time);
    
    auto results = join.process_join(left, right);
    
    EXPECT_GT(results.size(), 0);
}

TEST_F(StreamJoinTest, NoOverlap) {
    AlgorithmConfig config;
    config["window_size"] = "2000";
    config["max_delay"] = "1000";
    StreamJoin join(config);
    
    auto left = create_stream(5, base_time);
    auto right = create_stream(5, base_time + 20000); // Far in future
    
    auto results = join.process_join(left, right);
    
    // Should have no joins due to no overlap
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, EmptyStreams) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    std::vector<TimeSeriesData> left, right;
    
    auto results = join.process_join(left, right);
    
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, OneEmptyStream) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    auto left = create_stream(10, base_time);
    std::vector<TimeSeriesData> right;
    
    auto results = join.process_join(left, right);
    
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, Statistics) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time);
    
    join.process_join(left, right);
    
    auto stats = join.get_stats();
    
    EXPECT_GT(stats.at("total_joined"), 0);
}

TEST_F(StreamJoinTest, DifferentStreamSizes) {
    AlgorithmConfig config;
    config["window_size"] = "5000";
    config["max_delay"] = "2000";
    StreamJoin join(config);
    
    auto left = create_stream(20, base_time);
    auto right = create_stream(5, base_time);
    
    auto results = join.process_join(left, right);
    
    // Should handle different sizes
    EXPECT_GT(results.size(), 0);
    EXPECT_LE(results.size(), 20);
}
