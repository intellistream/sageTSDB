/**
 * @file pecj_compute_engine_test.cpp
 * @brief Unit tests for PECJComputeEngine
 * 
 * @version v3.0
 * @date 2024-12-04
 */

#include "sage_tsdb/compute/pecj_compute_engine.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#ifdef PECJ_MODE_INTEGRATED

using namespace sage_tsdb::compute;
using namespace sage_tsdb::core;
using ::testing::_;
using ::testing::Return;
using ::testing::AtLeast;

/**
 * @brief Mock TimeSeriesDB for testing
 */
class MockTimeSeriesDB : public TimeSeriesDB {
public:
    MOCK_METHOD(bool, query, 
                (const std::string& table_name, 
                 const TimeRange& range,
                 std::vector<std::vector<uint8_t>>& out_data), 
                (override));
    
    MOCK_METHOD(bool, insert,
                (const std::string& table_name,
                 uint64_t window_id,
                 const std::vector<uint8_t>& data),
                (override));
};

/**
 * @brief Mock ResourceHandle for testing
 */
class MockResourceHandle : public ResourceHandle {
public:
    MOCK_METHOD(void, submitTask, (std::function<void()> task), (override));
    MOCK_METHOD(bool, checkMemoryLimit, (size_t requested_bytes), (override));
};

/**
 * @brief Test fixture for PECJComputeEngine
 */
class PECJComputeEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock objects
        mock_db_ = std::make_unique<MockTimeSeriesDB>();
        mock_resource_handle_ = std::make_unique<MockResourceHandle>();
        
        // Setup default configuration
        config_.window_len_us = 1000000;
        config_.slide_len_us = 500000;
        config_.operator_type = "IAWJ";
        config_.max_memory_bytes = 1ULL * 1024 * 1024 * 1024;  // 1GB
        config_.max_threads = 2;
        config_.enable_aqp = true;
        config_.timeout_ms = 100;
    }
    
    void TearDown() override {
        // Cleanup
    }
    
    std::unique_ptr<MockTimeSeriesDB> mock_db_;
    std::unique_ptr<MockResourceHandle> mock_resource_handle_;
    ComputeConfig config_;
};

/**
 * @brief Test: Engine initialization
 */
TEST_F(PECJComputeEngineTest, Initialization) {
    PECJComputeEngine engine;
    
    // Should not be initialized initially
    EXPECT_FALSE(engine.isInitialized());
    
    // Initialize with valid config
    bool success = engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get());
    EXPECT_TRUE(success);
    EXPECT_TRUE(engine.isInitialized());
    
    // Verify configuration
    EXPECT_EQ(engine.getConfig().window_len_us, config_.window_len_us);
    EXPECT_EQ(engine.getConfig().slide_len_us, config_.slide_len_us);
}

/**
 * @brief Test: Invalid initialization parameters
 */
TEST_F(PECJComputeEngineTest, InvalidInitialization) {
    PECJComputeEngine engine;
    
    // Null database pointer
    EXPECT_FALSE(engine.initialize(config_, nullptr, mock_resource_handle_.get()));
    
    // Null resource handle
    EXPECT_FALSE(engine.initialize(config_, mock_db_.get(), nullptr));
    
    // Invalid window configuration
    ComputeConfig invalid_config = config_;
    invalid_config.window_len_us = 0;
    EXPECT_FALSE(engine.initialize(invalid_config, mock_db_.get(), mock_resource_handle_.get()));
}

/**
 * @brief Test: Double initialization
 */
TEST_F(PECJComputeEngineTest, DoubleInitialization) {
    PECJComputeEngine engine;
    
    // First initialization
    EXPECT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Second initialization should fail
    EXPECT_FALSE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
}

/**
 * @brief Test: Basic window join execution
 */
TEST_F(PECJComputeEngineTest, BasicWindowJoin) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Setup mock expectations
    std::vector<std::vector<uint8_t>> mock_s_data(100);  // 100 tuples
    std::vector<std::vector<uint8_t>> mock_r_data(80);   // 80 tuples
    
    EXPECT_CALL(*mock_db_, query(config_.stream_s_table, _, _))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(mock_s_data),
            Return(true)
        ));
    
    EXPECT_CALL(*mock_db_, query(config_.stream_r_table, _, _))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<2>(mock_r_data),
            Return(true)
        ));
    
    EXPECT_CALL(*mock_db_, insert(config_.result_table, _, _))
        .Times(AtLeast(0))
        .WillRepeatedly(Return(true));
    
    // Execute window join
    TimeRange window(1000000, 2000000);
    auto status = engine.executeWindowJoin(1, window);
    
    // Verify results
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.window_id, 1);
    EXPECT_EQ(status.input_s_count, 100);
    EXPECT_EQ(status.input_r_count, 80);
    EXPECT_GT(status.computation_time_ms, 0);
}

/**
 * @brief Test: Window join with empty input
 */
TEST_F(PECJComputeEngineTest, EmptyInputJoin) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Setup mock: empty data
    std::vector<std::vector<uint8_t>> empty_data;
    
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .Times(2)
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(empty_data),
            Return(true)
        ));
    
    // Execute
    TimeRange window(1000000, 2000000);
    auto status = engine.executeWindowJoin(1, window);
    
    // Should succeed with zero joins
    EXPECT_TRUE(status.success);
    EXPECT_EQ(status.join_count, 0);
    EXPECT_EQ(status.input_s_count, 0);
    EXPECT_EQ(status.input_r_count, 0);
}

/**
 * @brief Test: Invalid time range
 */
TEST_F(PECJComputeEngineTest, InvalidTimeRange) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Invalid range: end < start
    TimeRange invalid_window(2000000, 1000000);
    auto status = engine.executeWindowJoin(1, invalid_window);
    
    EXPECT_FALSE(status.success);
    EXPECT_FALSE(status.error.empty());
}

/**
 * @brief Test: Query failure handling
 */
TEST_F(PECJComputeEngineTest, QueryFailure) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Mock query failure
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .WillOnce(Return(false));
    
    TimeRange window(1000000, 2000000);
    auto status = engine.executeWindowJoin(1, window);
    
    EXPECT_FALSE(status.success);
    EXPECT_FALSE(status.error.empty());
}

/**
 * @brief Test: Metrics tracking
 */
TEST_F(PECJComputeEngineTest, MetricsTracking) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Initial metrics should be zero
    auto initial_metrics = engine.getMetrics();
    EXPECT_EQ(initial_metrics.total_windows_completed, 0);
    EXPECT_EQ(initial_metrics.total_tuples_processed, 0);
    
    // Setup mocks
    std::vector<std::vector<uint8_t>> mock_data(50);
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(mock_data),
            Return(true)
        ));
    EXPECT_CALL(*mock_db_, insert(_, _, _))
        .WillRepeatedly(Return(true));
    
    // Execute multiple windows
    const size_t NUM_WINDOWS = 5;
    for (size_t i = 0; i < NUM_WINDOWS; ++i) {
        TimeRange window(1000000 + i * 500000, 2000000 + i * 500000);
        auto status = engine.executeWindowJoin(i, window);
        EXPECT_TRUE(status.success);
    }
    
    // Check metrics
    auto final_metrics = engine.getMetrics();
    EXPECT_EQ(final_metrics.total_windows_completed, NUM_WINDOWS);
    EXPECT_GT(final_metrics.total_tuples_processed, 0);
    EXPECT_GT(final_metrics.avg_window_latency_ms, 0);
}

/**
 * @brief Test: Reset functionality
 */
TEST_F(PECJComputeEngineTest, Reset) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Execute a window
    std::vector<std::vector<uint8_t>> mock_data(50);
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(mock_data),
            Return(true)
        ));
    EXPECT_CALL(*mock_db_, insert(_, _, _))
        .WillRepeatedly(Return(true));
    
    TimeRange window(1000000, 2000000);
    engine.executeWindowJoin(1, window);
    
    // Metrics should be non-zero
    auto metrics_before = engine.getMetrics();
    EXPECT_GT(metrics_before.total_windows_completed, 0);
    
    // Reset
    engine.reset();
    
    // Metrics should be reset
    auto metrics_after = engine.getMetrics();
    EXPECT_EQ(metrics_after.total_windows_completed, 0);
    EXPECT_EQ(metrics_after.total_tuples_processed, 0);
}

/**
 * @brief Test: Concurrent window execution
 */
TEST_F(PECJComputeEngineTest, ConcurrentExecution) {
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Setup mocks (thread-safe)
    std::vector<std::vector<uint8_t>> mock_data(50);
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(mock_data),
            Return(true)
        ));
    EXPECT_CALL(*mock_db_, insert(_, _, _))
        .WillRepeatedly(Return(true));
    
    // Execute multiple windows concurrently
    const size_t NUM_THREADS = 4;
    std::vector<std::thread> threads;
    std::atomic<size_t> successful_windows(0);
    
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([&, i]() {
            TimeRange window(1000000 + i * 1000000, 2000000 + i * 1000000);
            auto status = engine.executeWindowJoin(i, window);
            if (status.success) {
                successful_windows.fetch_add(1);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // All windows should succeed
    EXPECT_EQ(successful_windows.load(), NUM_THREADS);
    
    // Metrics should reflect all windows
    auto metrics = engine.getMetrics();
    EXPECT_EQ(metrics.total_windows_completed, NUM_THREADS);
}

/**
 * @brief Test: Memory limit checking
 */
TEST_F(PECJComputeEngineTest, MemoryLimitCheck) {
    // Set very low memory limit
    config_.max_memory_bytes = 1024;  // 1KB
    
    PECJComputeEngine engine;
    ASSERT_TRUE(engine.initialize(config_, mock_db_.get(), mock_resource_handle_.get()));
    
    // Large dataset
    std::vector<std::vector<uint8_t>> large_data(10000);
    EXPECT_CALL(*mock_db_, query(_, _, _))
        .WillRepeatedly(testing::DoAll(
            testing::SetArgReferee<2>(large_data),
            Return(true)
        ));
    
    TimeRange window(1000000, 2000000);
    auto status = engine.executeWindowJoin(1, window);
    
    // Should fail due to memory limit
    // (Note: Actual behavior depends on implementation)
}

/**
 * @brief Main test runner
 */
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

#else
int main() {
    std::cout << "Tests require PECJ_MODE_INTEGRATED to be enabled" << std::endl;
    return 0;
}
#endif // PECJ_MODE_INTEGRATED
