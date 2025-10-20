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
            stream.push_back({start_time + i * interval, static_cast<double>(i)});
        }
        return stream;
    }
};

TEST_F(StreamJoinTest, BasicJoin) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time);
    
    auto results = join.process(left, right);
    
    EXPECT_GT(results.size(), 0);
}

TEST_F(StreamJoinTest, JoinWithTimeOffset) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time + 500); // 500ms offset
    
    auto results = join.process(left, right);
    
    // Should find joins despite offset
    EXPECT_GT(results.size(), 0);
    
    // Verify time differences are within window
    for (const auto& [l, r] : results) {
        EXPECT_LE(std::abs(l.timestamp - r.timestamp), 5000);
    }
}

TEST_F(StreamJoinTest, OutOfOrderData) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "3000"}});
    
    // Create out-of-order left stream
    std::vector<TimeSeriesData> left = {
        {base_time + 5000, 5.0},
        {base_time + 2000, 2.0},
        {base_time + 7000, 7.0},
        {base_time + 1000, 1.0},
        {base_time + 4000, 4.0}
    };
    
    auto right = create_stream(10, base_time);
    
    auto results = join.process(left, right);
    
    EXPECT_GT(results.size(), 0);
}

TEST_F(StreamJoinTest, NoOverlap) {
    StreamJoin join;
    join.configure({{"window_size", "2000"}, {"max_delay", "1000"}});
    
    auto left = create_stream(5, base_time);
    auto right = create_stream(5, base_time + 20000); // Far in future
    
    auto results = join.process(left, right);
    
    // Should have no joins due to no overlap
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, EmptyStreams) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    std::vector<TimeSeriesData> left, right;
    
    auto results = join.process(left, right);
    
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, OneEmptyStream) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    auto left = create_stream(10, base_time);
    std::vector<TimeSeriesData> right;
    
    auto results = join.process(left, right);
    
    EXPECT_EQ(results.size(), 0);
}

TEST_F(StreamJoinTest, Statistics) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    auto left = create_stream(10, base_time);
    auto right = create_stream(10, base_time);
    
    join.process(left, right);
    
    auto stats = join.get_stats();
    
    EXPECT_GT(stats.at("total_left_processed"), 0);
    EXPECT_GT(stats.at("total_right_processed"), 0);
    EXPECT_GT(stats.at("total_joined_pairs"), 0);
}

TEST_F(StreamJoinTest, WatermarkProgression) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    // Process in batches to test watermark
    for (int batch = 0; batch < 5; ++batch) {
        auto left = create_stream(5, base_time + batch * 10000);
        auto right = create_stream(5, base_time + batch * 10000);
        
        join.process(left, right);
    }
    
    auto stats = join.get_stats();
    
    // Watermark should have progressed
    EXPECT_GT(stats.at("left_watermark"), base_time);
    EXPECT_GT(stats.at("right_watermark"), base_time);
}

TEST_F(StreamJoinTest, DifferentStreamSizes) {
    StreamJoin join;
    join.configure({{"window_size", "5000"}, {"max_delay", "2000"}});
    
    auto left = create_stream(20, base_time);
    auto right = create_stream(5, base_time);
    
    auto results = join.process(left, right);
    
    // Should handle different sizes
    EXPECT_GT(results.size(), 0);
    EXPECT_LE(results.size(), 5); // Limited by smaller stream
}
