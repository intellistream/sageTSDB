/**
 * @file test_window_scheduler_simple.cpp
 * @brief Simple unit tests for WindowScheduler (basic structure)
 */

#include <gtest/gtest.h>

#ifdef PECJ_MODE_INTEGRATED

#include "sage_tsdb/compute/window_scheduler.h"
#include "sage_tsdb/compute/pecj_compute_engine.h"

using namespace sage_tsdb;
using namespace sage_tsdb::compute;

// Test TimeRange operations
TEST(TimeRangeTest, BasicOperations) {
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

// Test WindowSchedulerConfig default values
TEST(WindowSchedulerConfigTest, Defaults) {
    WindowSchedulerConfig config;
    
    // Check default values are reasonable
    EXPECT_GT(config.window_len_us, 0);
    EXPECT_GT(config.slide_len_us, 0);
    EXPECT_LE(config.slide_len_us, config.window_len_us);
}

// Test WindowInfo initialization (values may be uninitialized)
TEST(WindowInfoTest, Initialization) {
    WindowInfo window = {};  // Zero-initialize
    
    EXPECT_EQ(window.window_id, 0);
    EXPECT_FALSE(window.is_ready);
    EXPECT_FALSE(window.is_computing);
    EXPECT_FALSE(window.is_completed);
    EXPECT_EQ(window.stream_s_count, 0);
    EXPECT_EQ(window.stream_r_count, 0);
}

// Test SchedulingMetrics initialization
TEST(SchedulingMetricsTest, Initialization) {
    SchedulingMetrics metrics;
    
    EXPECT_EQ(metrics.total_windows_scheduled, 0);
    EXPECT_EQ(metrics.total_windows_completed, 0);
    EXPECT_EQ(metrics.total_windows_failed, 0);
    EXPECT_EQ(metrics.pending_windows, 0);
    EXPECT_EQ(metrics.active_windows, 0);
}

#endif // PECJ_MODE_INTEGRATED

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
