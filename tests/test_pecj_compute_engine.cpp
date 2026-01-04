/**
 * @file test_pecj_compute_engine.cpp
 * @brief Integration test for PECJComputeEngine with all operator types
 * 
 * This test verifies that sageTSDB's PECJComputeEngine can:
 * 1. Initialize with different operator types
 * 2. Create PECJ operators correctly
 * 3. Execute window joins
 * 4. Retrieve AQP results when supported
 * 
 * Note: This test requires PECJ_MODE_INTEGRATED and PECJ_FULL_INTEGRATION
 * to be defined for full functionality.
 * 
 * @version 1.0
 * @date 2024-12-16
 */

#include <gtest/gtest.h>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>

#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/time_series_db.h"

using namespace sage_tsdb;
using namespace sage_tsdb::compute;

// ============================================================================
// Test Fixture for PECJ Compute Engine
// ============================================================================

class PECJComputeEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a TimeSeriesDB instance for testing
        db_ = std::make_unique<TimeSeriesDB>();
        
        // Create test streams
        db_->createTable("stream_s");
        db_->createTable("stream_r");
        db_->createTable("join_results");
    }
    
    void TearDown() override {
        db_.reset();
    }
    
    // Helper to create config for a specific operator
    ComputeConfig createConfig(const std::string& operator_type) {
        ComputeConfig config;
        config.operator_type = operator_type;
        config.window_len_us = 1000000;  // 1 second
        config.slide_len_us = 500000;    // 0.5 second
        config.s_buffer_len = 10000;
        config.r_buffer_len = 10000;
        config.time_step_us = 1000;
        config.watermark_tag = "arrival";
        config.watermark_time_ms = 100;
        config.lateness_ms = 50;
        config.stream_s_table = "stream_s";
        config.stream_r_table = "stream_r";
        config.result_table = "join_results";
        return config;
    }
    
    // Helper to insert test data
    void insertTestData(int64_t base_timestamp, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            TimeSeriesData s_data;
            s_data.timestamp = base_timestamp + i * 1000;  // 1ms apart
            s_data.tags["key"] = std::to_string(i % 10);   // 10 different keys
            s_data.fields["value"] = std::to_string(100.0 + i);
            db_->insert("stream_s", s_data);
            
            TimeSeriesData r_data;
            r_data.timestamp = base_timestamp + i * 1000 + 500;  // 0.5ms offset
            r_data.tags["key"] = std::to_string(i % 10);
            r_data.fields["value"] = std::to_string(200.0 + i);
            db_->insert("stream_r", r_data);
        }
    }
    
    std::unique_ptr<TimeSeriesDB> db_;
};

// ============================================================================
// Initialization Tests for Each Operator Type
// ============================================================================

TEST_F(PECJComputeEngineTest, InitializeWithIAWJ) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with IAWJ operator";
    EXPECT_TRUE(engine.isInitialized());
    EXPECT_EQ(engine.getConfig().operator_type, "IAWJ");
#else
    // In stub mode, initialization should still succeed
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithMeanAQP) {
    PECJComputeEngine engine;
    auto config = createConfig("MeanAQP");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with MeanAQP operator";
    EXPECT_TRUE(operatorSupportsAQP(stringToOperatorType("MeanAQP")));
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithIMA) {
    PECJComputeEngine engine;
    auto config = createConfig("IMA");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with IMA operator";
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithMSWJ) {
    PECJComputeEngine engine;
    auto config = createConfig("MSWJ");
    config.mswj_compensation = true;
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with MSWJ operator";
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithIAWJSel) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJSel");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with IAWJSel operator";
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithLazyIAWJSel) {
    PECJComputeEngine engine;
    auto config = createConfig("LazyIAWJSel");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with LazyIAWJSel operator";
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithSHJ) {
    PECJComputeEngine engine;
    auto config = createConfig("SHJ");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with SHJ operator";
    EXPECT_FALSE(operatorSupportsAQP(stringToOperatorType("SHJ")));
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithPRJ) {
    PECJComputeEngine engine;
    auto config = createConfig("PRJ");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with PRJ operator";
#else
    EXPECT_TRUE(result);
#endif
}

TEST_F(PECJComputeEngineTest, InitializeWithPECJ) {
    PECJComputeEngine engine;
    auto config = createConfig("PECJ");
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result) << "Should initialize with PECJ operator";
#else
    EXPECT_TRUE(result);
#endif
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(PECJComputeEngineTest, InitializeWithNullDB) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    
    bool result = engine.initialize(config, nullptr, nullptr);
    EXPECT_FALSE(result) << "Should fail with null DB";
}

TEST_F(PECJComputeEngineTest, InitializeWithInvalidWindowParams) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    config.window_len_us = 0;  // Invalid
    
    bool result = engine.initialize(config, db_.get(), nullptr);
    EXPECT_FALSE(result) << "Should fail with invalid window parameters";
}

TEST_F(PECJComputeEngineTest, DoubleInitialization) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    
    bool result1 = engine.initialize(config, db_.get(), nullptr);
    bool result2 = engine.initialize(config, db_.get(), nullptr);
    
#ifdef PECJ_FULL_INTEGRATION
    EXPECT_TRUE(result1);
    EXPECT_FALSE(result2) << "Second initialization should fail";
#else
    EXPECT_TRUE(result1);
    EXPECT_FALSE(result2);
#endif
}

// ============================================================================
// Window Join Execution Tests
// ============================================================================

#ifdef PECJ_FULL_INTEGRATION

TEST_F(PECJComputeEngineTest, ExecuteWindowJoinBasic) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    engine.initialize(config, db_.get(), nullptr);
    
    // Insert test data
    int64_t base_ts = 1000000;
    insertTestData(base_ts, 100);
    
    // Execute window join
    compute::TimeRange time_range(base_ts, base_ts + 1000000);
    auto status = engine.executeWindowJoin(1, time_range);
    
    EXPECT_TRUE(status.success) << "Window join should succeed: " << status.error;
    EXPECT_GT(status.input_s_count, 0) << "Should have S tuples";
    EXPECT_GT(status.input_r_count, 0) << "Should have R tuples";
}

TEST_F(PECJComputeEngineTest, ExecuteWindowJoinWithAQPOperator) {
    PECJComputeEngine engine;
    auto config = createConfig("MeanAQP");
    engine.initialize(config, db_.get(), nullptr);
    
    // Insert test data
    int64_t base_ts = 1000000;
    insertTestData(base_ts, 100);
    
    // Execute window join
    compute::TimeRange time_range(base_ts, base_ts + 1000000);
    auto status = engine.executeWindowJoin(1, time_range);
    
    EXPECT_TRUE(status.success);
    EXPECT_TRUE(status.used_aqp) << "AQP should be used for MeanAQP operator";
}

TEST_F(PECJComputeEngineTest, ExecuteWindowJoinInvalidRange) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    engine.initialize(config, db_.get(), nullptr);
    
    // Invalid time range (end before start)
    compute::TimeRange invalid_range(2000000, 1000000);
    auto status = engine.executeWindowJoin(1, invalid_range);
    
    EXPECT_FALSE(status.success) << "Should fail with invalid time range";
}

TEST_F(PECJComputeEngineTest, ExecuteWithoutInitialization) {
    PECJComputeEngine engine;
    // Don't initialize
    
    compute::TimeRange time_range(1000000, 2000000);
    auto status = engine.executeWindowJoin(1, time_range);
    
    EXPECT_FALSE(status.success) << "Should fail without initialization";
    EXPECT_FALSE(status.error.empty());
}

#endif // PECJ_FULL_INTEGRATION

// ============================================================================
// Metrics Tests
// ============================================================================

TEST_F(PECJComputeEngineTest, GetMetricsAfterInitialization) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    engine.initialize(config, db_.get(), nullptr);
    
    auto metrics = engine.getMetrics();
    EXPECT_EQ(metrics.total_windows_completed, 0);
    EXPECT_EQ(metrics.total_tuples_processed, 0);
    EXPECT_EQ(metrics.failed_windows, 0);
}

TEST_F(PECJComputeEngineTest, ResetEngine) {
    PECJComputeEngine engine;
    auto config = createConfig("IAWJ");
    engine.initialize(config, db_.get(), nullptr);
    
    engine.reset();
    
    auto metrics = engine.getMetrics();
    EXPECT_EQ(metrics.total_windows_completed, 0);
}

// ============================================================================
// All Operators Initialization Test
// ============================================================================

TEST_F(PECJComputeEngineTest, InitializeAllOperatorTypes) {
    std::vector<std::string> operator_types = {
        "IAWJ", "MeanAQP", "IMA", "MSWJ", "AI", 
        "LinearSVI", "IAWJSel", "LazyIAWJSel", "SHJ", "PRJ", "PECJ"
    };
    
    for (const auto& op_type : operator_types) {
        PECJComputeEngine engine;
        auto config = createConfig(op_type);
        
        bool result = engine.initialize(config, db_.get(), nullptr);
        
        // In stub mode, all should succeed
        // In full integration mode, success depends on PECJ library
        std::cout << "  Operator " << op_type << ": " 
                  << (result ? "OK" : "FAILED") << std::endl;
        
#ifndef PECJ_FULL_INTEGRATION
        EXPECT_TRUE(result) << "Stub mode should succeed for " << op_type;
#endif
    }
}

#else // !PECJ_MODE_INTEGRATED

// Placeholder test when PECJ mode is not integrated
TEST(PECJComputeEngineStubTest, NotIntegrated) {
    std::cout << "PECJComputeEngine tests skipped - PECJ_MODE_INTEGRATED not defined" << std::endl;
    SUCCEED();
}

#endif // PECJ_MODE_INTEGRATED

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv) {
    std::cout << "==================================================" << std::endl;
    std::cout << "  PECJ Compute Engine Integration Tests" << std::endl;
    std::cout << "==================================================" << std::endl;
    
#ifdef PECJ_MODE_INTEGRATED
    std::cout << "  Mode: PECJ_MODE_INTEGRATED" << std::endl;
#ifdef PECJ_FULL_INTEGRATION
    std::cout << "  Full PECJ Integration: ENABLED" << std::endl;
#else
    std::cout << "  Full PECJ Integration: STUB MODE" << std::endl;
#endif
#else
    std::cout << "  Mode: STANDALONE (no PECJ integration)" << std::endl;
#endif
    std::cout << "==================================================" << std::endl;
    
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
