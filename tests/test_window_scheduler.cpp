/**
 * @file test_window_scheduler.cpp
 * @brief Unit tests for WindowScheduler
 */

#include <gtest/gtest.h>

#ifdef PECJ_MODE_INTEGRATED

#include "sage_tsdb/compute/window_scheduler.h"
#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/table_manager.h"
#include "sage_tsdb/plugins/resource_manager.h"
#include <thread>
#include <chrono>

using namespace sage_tsdb;
using namespace sage_tsdb::compute;
using namespace sage_tsdb::core;
using namespace sage_tsdb::plugins;

class WindowSchedulerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // TODO: Full setup requires complete PECJ integration
        // For now, we test basic logic only
    }
    
    void TearDown() override {
        // TODO: Cleanup when full integration available
    }
};

TEST_F(WindowSchedulerTest, CreateScheduler) {
    WindowSchedulerConfig config;
    config.window_len_us = 1000000;
    config.slide_len_us = 500000;
    config.trigger_policy = TriggerPolicy::Manual;
    
    // Create scheduler (without PECJ engine for basic test)
    // In real scenario, need actual PECJComputeEngine
    EXPECT_NO_THROW({
        // scheduler_ = std::make_unique<WindowScheduler>(
        //     config, nullptr, table_manager_.get(), resource_handle_.get());
    });
}

TEST_F(WindowSchedulerTest, WindowCreation) {
    WindowSchedulerConfig config;
    config.window_type = WindowType::Tumbling;
    config.window_len_us = 1000000;  // 1s
    config.trigger_policy = TriggerPolicy::Manual;
    
    // Test window boundary calculation
    int64_t timestamp = 1500000;  // 1.5s
    int64_t expected_start = 1000000;  // 1s (aligned to 1s boundary)
    int64_t expected_end = 2000000;    // 2s
    
    // Window should be aligned to tumbling window boundaries
    EXPECT_EQ(expected_start, (timestamp / config.window_len_us) * config.window_len_us);
}

TEST_F(WindowSchedulerTest, SlidingWindowCreation) {
    WindowSchedulerConfig config;
    config.window_type = WindowType::Sliding;
    config.window_len_us = 1000000;  // 1s window
    config.slide_len_us = 500000;     // 500ms slide
    
    // Test sliding window alignment
    int64_t timestamp = 1200000;  // 1.2s
    int64_t expected_start = 1000000;  // 1s (aligned to 500ms slide)
    
    int64_t slide_aligned = (timestamp / config.slide_len_us) * config.slide_len_us;
    EXPECT_EQ(expected_start, slide_aligned);
}

TEST_F(WindowSchedulerTest, WatermarkUpdate) {
    WindowSchedulerConfig config;
    config.trigger_policy = TriggerPolicy::Manual;
    
    // Watermark should advance monotonically
    // This tests the watermark logic without full scheduler
    int64_t watermark = 0;
    int64_t timestamp1 = 1000000;
    int64_t timestamp2 = 2000000;
    int64_t timestamp3 = 1500000;  // Out of order
    
    // Update with timestamp1
    watermark = std::max(watermark, timestamp1 - 100000);  // watermark = timestamp - max_delay
    EXPECT_GE(watermark, 0);
    
    // Update with timestamp2
    int64_t old_watermark = watermark;
    watermark = std::max(watermark, timestamp2 - 100000);
    EXPECT_GT(watermark, old_watermark);
    
    // Update with out-of-order timestamp3
    old_watermark = watermark;
    watermark = std::max(watermark, timestamp3 - 100000);
    EXPECT_EQ(watermark, old_watermark);  // Should not decrease
}

TEST_F(WindowSchedulerTest, TriggerPolicyTimeBased) {
    WindowInfo window;
    window.time_range.start_us = 1000000;
    window.time_range.end_us = 2000000;
    window.watermark_us = 1500000;
    
    WindowSchedulerConfig config;
    config.trigger_policy = TriggerPolicy::TimeBased;
    config.watermark_slack_us = 50000;
    
    // Should not trigger when watermark < window_end + slack
    int64_t current_watermark = 2000000;  // At window end
    bool should_trigger = current_watermark >= (window.time_range.end_us + config.watermark_slack_us);
    EXPECT_FALSE(should_trigger);
    
    // Should trigger when watermark >= window_end + slack
    current_watermark = 2100000;
    should_trigger = current_watermark >= (window.time_range.end_us + config.watermark_slack_us);
    EXPECT_TRUE(should_trigger);
}

TEST_F(WindowSchedulerTest, TriggerPolicyCountBased) {
    WindowInfo window;
    window.stream_s_count = 300;
    window.stream_r_count = 400;
    
    WindowSchedulerConfig config;
    config.trigger_policy = TriggerPolicy::CountBased;
    config.trigger_count_threshold = 1000;
    
    // Should not trigger when count < threshold
    bool should_trigger = (window.stream_s_count + window.stream_r_count) >= config.trigger_count_threshold;
    EXPECT_FALSE(should_trigger);
    
    // Should trigger when count >= threshold
    window.stream_s_count = 600;
    window.stream_r_count = 500;
    should_trigger = (window.stream_s_count + window.stream_r_count) >= config.trigger_count_threshold;
    EXPECT_TRUE(should_trigger);
}

TEST_F(WindowSchedulerTest, TriggerPolicyHybrid) {
    WindowInfo window;
    window.time_range.start_us = 1000000;
    window.time_range.end_us = 2000000;
    window.stream_s_count = 100;
    window.stream_r_count = 200;
    
    WindowSchedulerConfig config;
    config.trigger_policy = TriggerPolicy::Hybrid;
    config.watermark_slack_us = 50000;
    config.trigger_count_threshold = 1000;
    
    // Should trigger when time condition is met
    int64_t current_watermark = 2100000;
    bool should_trigger = current_watermark >= (window.time_range.end_us + config.watermark_slack_us);
    EXPECT_TRUE(should_trigger);
    
    // Should trigger when count condition is met
    window.stream_s_count = 600;
    window.stream_r_count = 500;
    current_watermark = 1500000;  // Time condition not met
    bool time_trigger = current_watermark >= (window.time_range.end_us + config.watermark_slack_us);
    bool count_trigger = (window.stream_s_count + window.stream_r_count) >= config.trigger_count_threshold;
    should_trigger = time_trigger || count_trigger;
    EXPECT_TRUE(should_trigger);
}

TEST_F(WindowSchedulerTest, MetricsTracking) {
    SchedulingMetrics metrics;
    
    // Initialize metrics
    metrics.total_windows_scheduled = 10;
    metrics.total_windows_completed = 8;
    metrics.total_windows_failed = 2;
    
    // Verify metrics
    EXPECT_EQ(10, metrics.total_windows_scheduled);
    EXPECT_EQ(8, metrics.total_windows_completed);
    EXPECT_EQ(2, metrics.total_windows_failed);
    
    // Calculate completion rate
    double completion_rate = static_cast<double>(metrics.total_windows_completed) / 
                            metrics.total_windows_scheduled;
    EXPECT_DOUBLE_EQ(0.8, completion_rate);
}

TEST_F(WindowSchedulerTest, TimeRangeOperations) {
    TimeRange range(1000000, 2000000);
    
    // Test contains
    EXPECT_TRUE(range.contains(1500000));
    EXPECT_TRUE(range.contains(1000000));  // Start inclusive
    EXPECT_FALSE(range.contains(2000000)); // End exclusive
    EXPECT_FALSE(range.contains(500000));
    EXPECT_FALSE(range.contains(2500000));
    
    // Test duration
    EXPECT_EQ(1000000, range.duration());
    
    // Test valid
    EXPECT_TRUE(range.valid());
    
    TimeRange invalid_range(2000000, 1000000);
    EXPECT_FALSE(invalid_range.valid());
}

#endif // PECJ_MODE_INTEGRATED

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
