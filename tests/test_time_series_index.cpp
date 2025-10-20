#include <gtest/gtest.h>
#include "sage_tsdb/core/time_series_index.h"
#include <thread>

using namespace sage_tsdb;

class TimeSeriesIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        index = std::make_unique<TimeSeriesIndex>();
        base_time = 1000000;
    }

    std::unique_ptr<TimeSeriesIndex> index;
    int64_t base_time;
};

TEST_F(TimeSeriesIndexTest, AddSingleDataPoint) {
    TimeSeriesData data{base_time, 42.5};
    size_t idx = index->add(data);
    
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(index->size(), 1);
}

TEST_F(TimeSeriesIndexTest, AddMultipleDataPoints) {
    for (int i = 0; i < 10; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    EXPECT_EQ(index->size(), 10);
}

TEST_F(TimeSeriesIndexTest, QueryByTimeRange) {
    // Add data points
    for (int i = 0; i < 20; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i * 10)};
        index->add(data);
    }
    
    // Query middle range
    TimeRange range{base_time + 5000, base_time + 14999};
    auto results = index->query(range);
    
    EXPECT_EQ(results.size(), 10); // Points at 5000, 6000, ..., 14000
    EXPECT_EQ(results[0].timestamp, base_time + 5000);
    EXPECT_EQ(results[9].timestamp, base_time + 14000);
}

TEST_F(TimeSeriesIndexTest, QueryWithTags) {
    // Add data with different tags
    for (int i = 0; i < 10; ++i) {
        std::unordered_map<std::string, std::string> tags;
        tags["sensor_id"] = "sensor_0" + std::to_string(i % 3);
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i), tags};
        index->add(data);
    }
    
    // Query for specific sensor
    TimeRange range{base_time, base_time + 20000};
    QueryConfig config;
    config.tags["sensor_id"] = "sensor_01";
    
    auto results = index->query(range, config);
    
    // Should get points at indices 1, 4, 7 (3 points)
    EXPECT_EQ(results.size(), 3);
    for (const auto& result : results) {
        EXPECT_EQ(result.tags.at("sensor_id"), "sensor_01");
    }
}

TEST_F(TimeSeriesIndexTest, QueryWithLimit) {
    // Add 100 data points
    for (int i = 0; i < 100; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    // Query with limit
    TimeRange range{base_time, base_time + 200000};
    QueryConfig config;
    config.limit = 10;
    
    auto results = index->query(range, config);
    
    EXPECT_EQ(results.size(), 10);
}

TEST_F(TimeSeriesIndexTest, QueryEmptyRange) {
    // Add data points
    for (int i = 0; i < 10; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    // Query range with no data
    TimeRange range{base_time + 50000, base_time + 60000};
    auto results = index->query(range);
    
    EXPECT_TRUE(results.empty());
}

TEST_F(TimeSeriesIndexTest, OutOfOrderInserts) {
    // Insert in reverse order
    for (int i = 9; i >= 0; --i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    EXPECT_EQ(index->size(), 10);
    
    // Query should return sorted results
    TimeRange range{base_time, base_time + 20000};
    auto results = index->query(range);
    
    EXPECT_EQ(results.size(), 10);
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_LE(results[i - 1].timestamp, results[i].timestamp);
    }
}

TEST_F(TimeSeriesIndexTest, ConcurrentReads) {
    // Add initial data
    for (int i = 0; i < 100; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    // Concurrent read threads
    std::vector<std::thread> threads;
    std::atomic<int> successful_reads{0};
    
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&]() {
            TimeRange range{base_time, base_time + 200000};
            auto results = index->query(range);
            if (results.size() == 100) {
                ++successful_reads;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(successful_reads, 10);
}

TEST_F(TimeSeriesIndexTest, Clear) {
    // Add data
    for (int i = 0; i < 10; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    EXPECT_EQ(index->size(), 10);
    
    // Clear
    index->clear();
    
    EXPECT_EQ(index->size(), 0);
    
    // Query should return empty
    TimeRange range{base_time, base_time + 20000};
    auto results = index->query(range);
    EXPECT_TRUE(results.empty());
}

TEST_F(TimeSeriesIndexTest, Statistics) {
    // Add data
    for (int i = 0; i < 50; ++i) {
        TimeSeriesData data{base_time + i * 1000, static_cast<double>(i)};
        index->add(data);
    }
    
    auto stats = index->get_stats();
    
    EXPECT_EQ(stats.at("total_data_points"), 50);
    EXPECT_GT(stats.at("total_queries"), 0); // From previous tests
}
