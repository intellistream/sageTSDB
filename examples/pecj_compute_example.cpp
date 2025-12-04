/**
 * @file pecj_compute_example.cpp
 * @brief Example usage of PECJComputeEngine in deep integration mode
 * 
 * This example demonstrates:
 * 1. Initializing the compute engine
 * 2. Writing data to sageTSDB tables
 * 3. Executing window joins
 * 4. Querying results
 * 5. Monitoring metrics
 */

#include "sage_tsdb/compute/pecj_compute_engine.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

#ifdef PECJ_MODE_INTEGRATED

using namespace sage_tsdb::compute;
using namespace sage_tsdb::core;

/**
 * @brief Generate sample stock order data
 */
std::vector<std::vector<uint8_t>> generateOrderData(int64_t start_ts, size_t count) {
    std::vector<std::vector<uint8_t>> data;
    data.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        // Mock order: [timestamp, order_id, symbol_id, quantity, price]
        struct Order {
            int64_t timestamp;
            uint64_t order_id;
            uint32_t symbol_id;
            uint32_t quantity;
            double price;
        };
        
        Order order;
        order.timestamp = start_ts + i * 1000;  // 1ms apart
        order.order_id = 1000 + i;
        order.symbol_id = i % 100;              // 100 different symbols
        order.quantity = 100 + (i % 1000);
        order.price = 100.0 + (i % 100) * 0.5;
        
        std::vector<uint8_t> bytes(sizeof(Order));
        std::memcpy(bytes.data(), &order, sizeof(Order));
        data.push_back(std::move(bytes));
    }
    
    return data;
}

/**
 * @brief Generate sample stock trade data
 */
std::vector<std::vector<uint8_t>> generateTradeData(int64_t start_ts, size_t count) {
    std::vector<std::vector<uint8_t>> data;
    data.reserve(count);
    
    for (size_t i = 0; i < count; ++i) {
        // Mock trade: [timestamp, trade_id, symbol_id, quantity, price]
        struct Trade {
            int64_t timestamp;
            uint64_t trade_id;
            uint32_t symbol_id;
            uint32_t quantity;
            double price;
        };
        
        Trade trade;
        trade.timestamp = start_ts + i * 800;   // Slightly different timing
        trade.trade_id = 2000 + i;
        trade.symbol_id = i % 100;
        trade.quantity = 50 + (i % 500);
        trade.price = 100.0 + (i % 100) * 0.5;
        
        std::vector<uint8_t> bytes(sizeof(Trade));
        std::memcpy(bytes.data(), &trade, sizeof(Trade));
        data.push_back(std::move(bytes));
    }
    
    return data;
}

/**
 * @brief Example 1: Basic window join
 */
void example1_basic_join(TimeSeriesDB* db, PECJComputeEngine& engine) {
    std::cout << "\n=== Example 1: Basic Window Join ===" << std::endl;
    
    // Define time window (1 second)
    int64_t start_ts = 1000000;  // 1 second
    int64_t end_ts = 2000000;    // 2 seconds
    TimeRange window(start_ts, end_ts);
    
    // Generate and insert data
    auto order_data = generateOrderData(start_ts, 1000);
    auto trade_data = generateTradeData(start_ts, 800);
    
    std::cout << "Inserting " << order_data.size() << " orders..." << std::endl;
    for (const auto& data : order_data) {
        db->insert("stream_s", 0, data);
    }
    
    std::cout << "Inserting " << trade_data.size() << " trades..." << std::endl;
    for (const auto& data : trade_data) {
        db->insert("stream_r", 0, data);
    }
    
    // Execute window join
    std::cout << "Executing window join..." << std::endl;
    auto status = engine.executeWindowJoin(1, window);
    
    // Display results
    if (status.success) {
        std::cout << "✓ Join completed successfully" << std::endl;
        std::cout << "  Window ID: " << status.window_id << std::endl;
        std::cout << "  Join count: " << status.join_count << std::endl;
        std::cout << "  Computation time: " << status.computation_time_ms << " ms" << std::endl;
        std::cout << "  Selectivity: " << status.selectivity * 100 << "%" << std::endl;
        std::cout << "  Input S: " << status.input_s_count << " tuples" << std::endl;
        std::cout << "  Input R: " << status.input_r_count << " tuples" << std::endl;
    } else {
        std::cout << "✗ Join failed: " << status.error << std::endl;
    }
}

/**
 * @brief Example 2: Continuous window processing
 */
void example2_continuous_windows(TimeSeriesDB* db, PECJComputeEngine& engine) {
    std::cout << "\n=== Example 2: Continuous Window Processing ===" << std::endl;
    
    const size_t NUM_WINDOWS = 5;
    const uint64_t WINDOW_LEN = 1000000;  // 1 second
    const uint64_t SLIDE_LEN = 500000;    // 500ms (50% overlap)
    
    int64_t base_ts = 1000000;
    
    for (size_t i = 0; i < NUM_WINDOWS; ++i) {
        int64_t start_ts = base_ts + i * SLIDE_LEN;
        int64_t end_ts = start_ts + WINDOW_LEN;
        
        std::cout << "\nWindow " << i << ": [" << start_ts << ", " << end_ts << ")" << std::endl;
        
        // Generate data for this window
        auto order_data = generateOrderData(start_ts, 500);
        auto trade_data = generateTradeData(start_ts, 400);
        
        for (const auto& data : order_data) {
            db->insert("stream_s", i, data);
        }
        for (const auto& data : trade_data) {
            db->insert("stream_r", i, data);
        }
        
        // Execute join
        TimeRange window(start_ts, end_ts);
        auto status = engine.executeWindowJoin(i, window);
        
        if (status.success) {
            std::cout << "  ✓ Joins: " << status.join_count 
                     << ", Time: " << status.computation_time_ms << "ms" << std::endl;
        } else {
            std::cout << "  ✗ Failed: " << status.error << std::endl;
        }
    }
    
    // Display cumulative metrics
    auto metrics = engine.getMetrics();
    std::cout << "\nCumulative Metrics:" << std::endl;
    std::cout << "  Total windows: " << metrics.total_windows_completed << std::endl;
    std::cout << "  Avg latency: " << metrics.avg_window_latency_ms << " ms" << std::endl;
    std::cout << "  P99 latency: " << metrics.p99_window_latency_ms << " ms" << std::endl;
}

/**
 * @brief Example 3: High throughput with parallel windows
 */
void example3_parallel_windows(TimeSeriesDB* db, PECJComputeEngine& engine,
                               ResourceHandle* resource_handle) {
    std::cout << "\n=== Example 3: Parallel Window Processing ===" << std::endl;
    
    const size_t NUM_PARALLEL_WINDOWS = 4;
    std::vector<std::thread> threads;
    std::atomic<size_t> completed(0);
    std::atomic<size_t> total_joins(0);
    
    for (size_t i = 0; i < NUM_PARALLEL_WINDOWS; ++i) {
        threads.emplace_back([&, i]() {
            int64_t start_ts = 1000000 + i * 1000000;
            int64_t end_ts = start_ts + 1000000;
            
            // Generate data
            auto order_data = generateOrderData(start_ts, 1000);
            auto trade_data = generateTradeData(start_ts, 800);
            
            for (const auto& data : order_data) {
                db->insert("stream_s", i, data);
            }
            for (const auto& data : trade_data) {
                db->insert("stream_r", i, data);
            }
            
            // Execute join
            TimeRange window(start_ts, end_ts);
            auto status = engine.executeWindowJoin(i, window);
            
            if (status.success) {
                completed.fetch_add(1);
                total_joins.fetch_add(status.join_count);
            }
        });
    }
    
    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Completed " << completed.load() << "/" << NUM_PARALLEL_WINDOWS 
             << " windows" << std::endl;
    std::cout << "Total joins: " << total_joins.load() << std::endl;
}

/**
 * @brief Example 4: AQP fallback demonstration
 */
void example4_aqp_fallback(TimeSeriesDB* db, PECJComputeEngine& engine) {
    std::cout << "\n=== Example 4: AQP Fallback ===" << std::endl;
    
    // Generate large dataset to trigger timeout
    int64_t start_ts = 1000000;
    int64_t end_ts = 2000000;
    TimeRange window(start_ts, end_ts);
    
    std::cout << "Generating large dataset..." << std::endl;
    auto order_data = generateOrderData(start_ts, 50000);
    auto trade_data = generateTradeData(start_ts, 50000);
    
    for (const auto& data : order_data) {
        db->insert("stream_s", 0, data);
    }
    for (const auto& data : trade_data) {
        db->insert("stream_r", 0, data);
    }
    
    std::cout << "Executing join (may timeout and use AQP)..." << std::endl;
    auto status = engine.executeWindowJoin(1, window);
    
    if (status.success) {
        if (status.used_aqp) {
            std::cout << "✓ Used AQP estimation" << std::endl;
            std::cout << "  Estimated joins: " << status.aqp_estimate << std::endl;
            std::cout << "  AQP error: " << status.aqp_error * 100 << "%" << std::endl;
        } else {
            std::cout << "✓ Exact join completed" << std::endl;
            std::cout << "  Join count: " << status.join_count << std::endl;
        }
        std::cout << "  Computation time: " << status.computation_time_ms << " ms" << std::endl;
    } else {
        std::cout << "✗ Join failed: " << status.error << std::endl;
    }
}

/**
 * @brief Main function
 */
int main(int argc, char** argv) {
    std::cout << "PECJ Compute Engine Example (Deep Integration Mode)" << std::endl;
    std::cout << "======================================================" << std::endl;
    
    // Initialize sageTSDB (mock)
    TimeSeriesDB db;
    // db.initialize(...);
    
    // Create resource handle (mock)
    ResourceRequest resource_req;
    resource_req.requested_threads = 4;
    resource_req.max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 2GB
    
    // auto resource_handle = db.getResourceManager()->allocateForCompute(
    //     "pecj_engine", resource_req);
    ResourceHandle* resource_handle = nullptr;  // Mock
    
    // Configure PECJ engine
    ComputeConfig config;
    config.window_len_us = 1000000;      // 1 second
    config.slide_len_us = 500000;        // 500ms
    config.operator_type = "IAWJ";
    config.max_delay_us = 100000;        // 100ms
    config.aqp_threshold = 0.05;         // 5% error
    config.max_memory_bytes = 2ULL * 1024 * 1024 * 1024;
    config.max_threads = 4;
    config.enable_aqp = true;
    config.timeout_ms = 1000;            // 1 second timeout
    
    // Initialize PECJ engine
    PECJComputeEngine engine;
    if (!engine.initialize(config, &db, resource_handle)) {
        std::cerr << "Failed to initialize PECJ engine" << std::endl;
        return 1;
    }
    
    std::cout << "✓ PECJ engine initialized successfully" << std::endl;
    
    // Run examples
    try {
        example1_basic_join(&db, engine);
        example2_continuous_windows(&db, engine);
        // example3_parallel_windows(&db, engine, resource_handle);
        // example4_aqp_fallback(&db, engine);
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    // Display final metrics
    std::cout << "\n=== Final Metrics ===" << std::endl;
    auto metrics = engine.getMetrics();
    std::cout << "Total windows completed: " << metrics.total_windows_completed << std::endl;
    std::cout << "Total tuples processed: " << metrics.total_tuples_processed << std::endl;
    std::cout << "Average throughput: " << metrics.avg_throughput_events_per_sec 
             << " events/sec" << std::endl;
    std::cout << "Average latency: " << metrics.avg_window_latency_ms << " ms" << std::endl;
    std::cout << "P99 latency: " << metrics.p99_window_latency_ms << " ms" << std::endl;
    std::cout << "Peak memory: " << metrics.peak_memory_bytes / 1024 / 1024 << " MB" << std::endl;
    std::cout << "Failed windows: " << metrics.failed_windows << std::endl;
    std::cout << "AQP invocations: " << metrics.aqp_invocations << std::endl;
    
    std::cout << "\n✓ All examples completed successfully" << std::endl;
    
    return 0;
}

#else
int main() {
    std::cout << "This example requires PECJ_MODE_INTEGRATED to be enabled" << std::endl;
    return 0;
}
#endif // PECJ_MODE_INTEGRATED
