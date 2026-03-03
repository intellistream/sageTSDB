/**
 * @file test_window_scheduler_simple.cpp
 * @brief Simple unit tests for WindowScheduler (basic structure)
 */

#include <gtest/gtest.h>

#ifdef PECJ_MODE_INTEGRATED

#include "sage_tsdb/compute/window_scheduler.h"
#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/resource_manager.h"

using namespace sage_tsdb;
using namespace sage_tsdb::compute;
using namespace sage_tsdb::core;

class WindowSchedulerRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        resource_manager_ = createResourceManager();
        ASSERT_NE(resource_manager_, nullptr);

        ResourceRequest request;
        request.requested_threads = 1;
        request.max_memory_bytes = 64ULL * 1024ULL * 1024ULL;
        resource_handle_ = resource_manager_->allocateForCompute("window_scheduler_test", request);
        ASSERT_NE(resource_handle_, nullptr);

        table_manager_ = reinterpret_cast<sage_tsdb::core::TableManager*>(0x1);
    }

    void TearDown() override {
        if (resource_manager_) {
            resource_manager_->releaseCompute("window_scheduler_test");
        }
        resource_handle_.reset();
        table_manager_ = nullptr;
        resource_manager_.reset();
    }

    std::shared_ptr<ResourceManager> resource_manager_;
    std::shared_ptr<ResourceHandle> resource_handle_;
    sage_tsdb::core::TableManager* table_manager_ = nullptr;
    PECJComputeEngine compute_engine_;
};

// Test TimeRange operations
TEST(TimeRangeTest, BasicOperations) {
    sage_tsdb::compute::TimeRange range(1000000, 2000000);
    
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
    
    sage_tsdb::compute::TimeRange invalid_range(2000000, 1000000);
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

TEST_F(WindowSchedulerRuntimeTest, RejectsNullDependencies) {
    WindowSchedulerConfig config;
    EXPECT_THROW(
        WindowScheduler(config, nullptr, table_manager_, resource_handle_.get()),
        std::invalid_argument
    );
    EXPECT_THROW(
        WindowScheduler(config, &compute_engine_, nullptr, resource_handle_.get()),
        std::invalid_argument
    );
    EXPECT_THROW(
        WindowScheduler(config, &compute_engine_, table_manager_, nullptr),
        std::invalid_argument
    );
}

TEST_F(WindowSchedulerRuntimeTest, ManualScheduleCreatesPendingWindow) {
    WindowSchedulerConfig config;
    config.trigger_policy = TriggerPolicy::Manual;

    WindowScheduler scheduler(config, &compute_engine_, table_manager_, resource_handle_.get());
    EXPECT_FALSE(scheduler.isRunning());
    EXPECT_EQ(scheduler.getPendingWindowCount(), 0U);

    sage_tsdb::compute::TimeRange range(1'000'000, 2'000'000);
    EXPECT_TRUE(scheduler.scheduleWindow(42, range));
    EXPECT_EQ(scheduler.getPendingWindowCount(), 1U);

    auto window = scheduler.getWindowInfo(42);
    EXPECT_EQ(window.window_id, 42U);
    EXPECT_EQ(window.time_range.start_us, 1'000'000);
    EXPECT_EQ(window.time_range.end_us, 2'000'000);
    EXPECT_TRUE(window.is_ready);
}

TEST_F(WindowSchedulerRuntimeTest, WatermarkMonotonicity) {
    WindowSchedulerConfig config;
    WindowScheduler scheduler(config, &compute_engine_, table_manager_, resource_handle_.get());

    scheduler.updateWatermark(1000);
    EXPECT_EQ(scheduler.getWatermark(), 1000);

    scheduler.updateWatermark(900);
    EXPECT_EQ(scheduler.getWatermark(), 1000);

    scheduler.updateWatermark(1200);
    EXPECT_EQ(scheduler.getWatermark(), 1200);
}

TEST_F(WindowSchedulerRuntimeTest, OnDataInsertedNoOpWhenStopped) {
    WindowSchedulerConfig config;
    WindowScheduler scheduler(config, &compute_engine_, table_manager_, resource_handle_.get());

    scheduler.onDataInserted("stream_s", 1'000'000, 2);
    EXPECT_TRUE(scheduler.getAllWindows().empty());
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
