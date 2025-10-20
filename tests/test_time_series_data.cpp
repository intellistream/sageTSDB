#include <gtest/gtest.h>
#include "sage_tsdb/core/time_series_data.h"
#include <chrono>

using namespace sage_tsdb;

class TimeSeriesDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        current_time = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
    }

    int64_t current_time;
};

TEST_F(TimeSeriesDataTest, BasicConstruction) {
    TimeSeriesData data{current_time, 42.5};
    
    EXPECT_EQ(data.timestamp, current_time);
    EXPECT_DOUBLE_EQ(data.value, 42.5);
    EXPECT_TRUE(data.tags.empty());
}

TEST_F(TimeSeriesDataTest, ConstructionWithTags) {
    std::unordered_map<std::string, std::string> tags = {
        {"sensor_id", "sensor_01"},
        {"location", "room_A"}
    };
    
    TimeSeriesData data{current_time, 25.3, tags};
    
    EXPECT_EQ(data.timestamp, current_time);
    EXPECT_DOUBLE_EQ(data.value, 25.3);
    EXPECT_EQ(data.tags.size(), 2);
    EXPECT_EQ(data.tags.at("sensor_id"), "sensor_01");
    EXPECT_EQ(data.tags.at("location"), "room_A");
}

TEST_F(TimeSeriesDataTest, TimeRangeContains) {
    TimeRange range{1000, 2000};
    
    EXPECT_FALSE(range.contains(999));
    EXPECT_TRUE(range.contains(1000));
    EXPECT_TRUE(range.contains(1500));
    EXPECT_TRUE(range.contains(2000));
    EXPECT_FALSE(range.contains(2001));
}

TEST_F(TimeSeriesDataTest, TimeRangeDuration) {
    TimeRange range{1000, 3000};
    EXPECT_EQ(range.duration(), 2000);
}

TEST_F(TimeSeriesDataTest, AggregationTypeToString) {
    EXPECT_EQ(aggregation_to_string(AggregationType::SUM), "sum");
    EXPECT_EQ(aggregation_to_string(AggregationType::AVG), "avg");
    EXPECT_EQ(aggregation_to_string(AggregationType::MIN), "min");
    EXPECT_EQ(aggregation_to_string(AggregationType::MAX), "max");
    EXPECT_EQ(aggregation_to_string(AggregationType::COUNT), "count");
    EXPECT_EQ(aggregation_to_string(AggregationType::STDDEV), "stddev");
}

TEST_F(TimeSeriesDataTest, StringToAggregationType) {
    EXPECT_EQ(string_to_aggregation("sum"), AggregationType::SUM);
    EXPECT_EQ(string_to_aggregation("avg"), AggregationType::AVG);
    EXPECT_EQ(string_to_aggregation("min"), AggregationType::MIN);
    EXPECT_EQ(string_to_aggregation("max"), AggregationType::MAX);
    EXPECT_EQ(string_to_aggregation("count"), AggregationType::COUNT);
    EXPECT_EQ(string_to_aggregation("stddev"), AggregationType::STDDEV);
    
    // Test case insensitivity
    EXPECT_EQ(string_to_aggregation("SUM"), AggregationType::SUM);
    EXPECT_EQ(string_to_aggregation("AVG"), AggregationType::AVG);
    
    // Test invalid input
    EXPECT_THROW(string_to_aggregation("invalid"), std::invalid_argument);
}

TEST_F(TimeSeriesDataTest, QueryConfigDefaults) {
    QueryConfig config;
    
    EXPECT_EQ(config.limit, 1000);
    EXPECT_EQ(config.aggregation, AggregationType::NONE);
    EXPECT_TRUE(config.tags.empty());
}

TEST_F(TimeSeriesDataTest, QueryConfigWithTags) {
    std::unordered_map<std::string, std::string> tags = {
        {"sensor_id", "sensor_02"}
    };
    
    QueryConfig config;
    config.tags = tags;
    config.limit = 500;
    config.aggregation = AggregationType::AVG;
    
    EXPECT_EQ(config.limit, 500);
    EXPECT_EQ(config.aggregation, AggregationType::AVG);
    EXPECT_EQ(config.tags.size(), 1);
    EXPECT_EQ(config.tags.at("sensor_id"), "sensor_02");
}

TEST_F(TimeSeriesDataTest, MultipleDataPoints) {
    std::vector<TimeSeriesData> data_points;
    
    for (int i = 0; i < 10; ++i) {
        data_points.push_back({current_time + i * 1000, static_cast<double>(i * 10)});
    }
    
    EXPECT_EQ(data_points.size(), 10);
    EXPECT_EQ(data_points[0].timestamp, current_time);
    EXPECT_DOUBLE_EQ(data_points[0].value, 0.0);
    EXPECT_EQ(data_points[9].timestamp, current_time + 9000);
    EXPECT_DOUBLE_EQ(data_points[9].value, 90.0);
}
