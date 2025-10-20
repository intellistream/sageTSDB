#include <gtest/gtest.h>
#include "sage_tsdb/core/time_series_db.h"

using namespace sage_tsdb;

class TimeSeriesDBTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<TimeSeriesDB>();
        base_time = 1000000;
    }

    std::unique_ptr<TimeSeriesDB> db;
    int64_t base_time;
};

TEST_F(TimeSeriesDBTest, AddAndQuery) {
    // Add data points
    for (int i = 0; i < 10; ++i) {
        sage_tsdb::Tags tags = {
            {"sensor_id", "sensor_01"}
        };
        size_t idx = db->add(base_time + i * 1000, static_cast<double>(i * 10), tags);
        EXPECT_EQ(idx, i);
    }
    
    // Query
    TimeRange range{base_time, base_time + 20000};
    QueryConfig config(range);
    auto results = db->query(config);
    
    EXPECT_EQ(results.size(), 10);
}

TEST_F(TimeSeriesDBTest, QueryWithTagFilter) {
    // Add data with different sensors
    for (int i = 0; i < 20; ++i) {
        sage_tsdb::Tags tags = {
            {"sensor_id", i % 2 == 0 ? "sensor_01" : "sensor_02"}
        };
        db->add(base_time + i * 1000, static_cast<double>(i), tags);
    }
    
    // Query for sensor_01 only
    TimeRange range{base_time, base_time + 30000};
    QueryConfig config(range);
    config.filter_tags["sensor_id"] = "sensor_01";
    
    auto results = db->query(config);
    
    EXPECT_EQ(results.size(), 10);
    for (const auto& result : results) {
        EXPECT_EQ(result.tags.at("sensor_id"), "sensor_01");
    }
}

TEST_F(TimeSeriesDBTest, MultipleTagFilters) {
    // Add data with multiple tags
    for (int i = 0; i < 20; ++i) {
        sage_tsdb::Tags tags = {
            {"sensor_id", i % 2 == 0 ? "sensor_01" : "sensor_02"},
            {"location", i % 4 < 2 ? "room_A" : "room_B"}
        };
        db->add(base_time + i * 1000, static_cast<double>(i), tags);
    }
    
    // Query for sensor_01 in room_A
    TimeRange range{base_time, base_time + 30000};
    QueryConfig config(range);
    config.filter_tags["sensor_id"] = "sensor_01";
    config.filter_tags["location"] = "room_A";
    
    auto results = db->query(config);
    
    // Should get 5 results (indices 0, 4, 8, 12, 16)
    EXPECT_EQ(results.size(), 5);
    for (const auto& result : results) {
        EXPECT_EQ(result.tags.at("sensor_id"), "sensor_01");
        EXPECT_EQ(result.tags.at("location"), "room_A");
    }
}

TEST_F(TimeSeriesDBTest, Clear) {
    // Add data
    for (int i = 0; i < 10; ++i) {
        db->add(base_time + i * 1000, static_cast<double>(i));
    }
    
    EXPECT_EQ(db->size(), 10);
    
    db->clear();
    
    EXPECT_EQ(db->size(), 0);
}

TEST_F(TimeSeriesDBTest, Statistics) {
    // Add some data
    for (int i = 0; i < 50; ++i) {
        db->add(base_time + i * 1000, static_cast<double>(i));
    }
    
    // Perform some queries
    TimeRange range{base_time, base_time + 30000};
    QueryConfig config(range);
    db->query(config);
    db->query(config);
    
    auto stats = db->get_stats();
    
    // Just verify stats map is not empty
    EXPECT_GT(stats.size(), 0);
    EXPECT_EQ(db->size(), 50);
}

TEST_F(TimeSeriesDBTest, EmptyDatabaseQuery) {
    TimeRange range{base_time, base_time + 10000};
    auto results = db->query(range);
    
    EXPECT_TRUE(results.empty());
}

TEST_F(TimeSeriesDBTest, LargeDataSet) {
    // Add 1000 data points
    for (int i = 0; i < 1000; ++i) {
        db->add(base_time + i * 1000, static_cast<double>(i));
    }
    
    EXPECT_EQ(db->size(), 1000);
    
    // Query subset
    TimeRange range{base_time + 100000, base_time + 200000};
    QueryConfig config(range);
    auto results = db->query(config);
    
    // May get 100 or 101 depending on boundary conditions
    EXPECT_GE(results.size(), 99);
    EXPECT_LE(results.size(), 102);
}
