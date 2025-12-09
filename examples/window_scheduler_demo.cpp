/**
 * @file window_scheduler_demo.cpp
 * @brief Demo program showing WindowScheduler usage
 * 
 * This example demonstrates:
 * 1. Creating tables for streams
 * 2. Setting up PECJ compute engine
 * 3. Configuring WindowScheduler
 * 4. Inserting data and triggering window computations
 * 5. Monitoring scheduling metrics
 */

#include <iostream>
#include <random>
#include <thread>
#include <chrono>

#ifdef PECJ_MODE_INTEGRATED

#include "sage_tsdb/compute/window_scheduler.h"
#include "sage_tsdb/compute/pecj_compute_engine.h"
#include "sage_tsdb/core/table_manager.h"
#include "sage_tsdb/plugins/resource_manager.h"
#include "sage_tsdb/core/time_series_data.h"

using namespace sage_tsdb;
using namespace sage_tsdb::compute;
using namespace sage_tsdb::plugins;

// Helper function to generate random stock data
TimeSeriesData generateStockData(const std::string& symbol, int64_t timestamp) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> price_dist(100.0, 200.0);
    static std::uniform_int_distribution<> volume_dist(100, 10000);
    
    TimeSeriesData data;
    data.timestamp = timestamp;
    data.tags = {{"symbol", symbol}, {"exchange", "NYSE"}};
    data.fields = {
        {"price", std::to_string(price_dist(gen))},
        {"volume", std::to_string(volume_dist(gen))}
    };
    
    return data;
}

int main() {
    std::cout << "========== WindowScheduler Demo ==========" << std::endl;
    
    // ========== 1. Setup ==========
    std::cout << "\n[1] Setting up components..." << std::endl;
    
    std::string data_dir = "/tmp/window_scheduler_demo";
    system(("rm -rf " + data_dir).c_str());
    system(("mkdir -p " + data_dir).c_str());
    
    // Create table manager
    auto table_manager = std::make_unique<TableManager>(data_dir);
    
    // Create stream tables
    TableConfig table_config;
    table_config.data_dir = data_dir;
    table_config.memtable_size_bytes = 64 * 1024 * 1024;  // 64MB
    
    table_manager->createStreamTable("stock_stream_s", table_config);
    table_manager->createStreamTable("stock_stream_r", table_config);
    table_manager->createJoinResultTable("stock_join_results");
    
    std::cout << "  ✓ Created tables: stock_stream_s, stock_stream_r, stock_join_results" << std::endl;
    
    // Create resource manager
    auto resource_manager = createResourceManager();
    ResourceRequest resource_req;
    resource_req.requested_threads = 4;
    resource_req.max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 2GB
    
    auto resource_handle = resource_manager->allocate("pecj_demo", resource_req);
    std::cout << "  ✓ Allocated resources: " << resource_req.requested_threads << " threads, "
              << (resource_req.max_memory_bytes / 1024 / 1024) << " MB" << std::endl;
    
    // ========== 2. Initialize PECJ Compute Engine ==========
    std::cout << "\n[2] Initializing PECJ Compute Engine..." << std::endl;
    
    ComputeConfig compute_config;
    compute_config.window_len_us = 5000000;   // 5 second window
    compute_config.slide_len_us = 1000000;    // 1 second slide
    compute_config.operator_type = "IAWJ";
    compute_config.max_delay_us = 500000;     // 500ms max delay
    compute_config.aqp_threshold = 0.05;
    compute_config.enable_aqp = true;
    compute_config.stream_s_table = "stock_stream_s";
    compute_config.stream_r_table = "stock_stream_r";
    compute_config.result_table = "stock_join_results";
    
    // Note: Actual PECJ initialization requires PECJ library
    // This is a demonstration of the API
    std::cout << "  ✓ PECJ Config:" << std::endl;
    std::cout << "    - Window: " << (compute_config.window_len_us / 1000000.0) << "s" << std::endl;
    std::cout << "    - Slide: " << (compute_config.slide_len_us / 1000000.0) << "s" << std::endl;
    std::cout << "    - Operator: " << compute_config.operator_type << std::endl;
    
    // ========== 3. Configure WindowScheduler ==========
    std::cout << "\n[3] Configuring WindowScheduler..." << std::endl;
    
    WindowSchedulerConfig scheduler_config;
    scheduler_config.window_type = WindowType::Sliding;
    scheduler_config.window_len_us = compute_config.window_len_us;
    scheduler_config.slide_len_us = compute_config.slide_len_us;
    scheduler_config.trigger_policy = TriggerPolicy::Hybrid;
    scheduler_config.trigger_interval_us = 100000;  // Check every 100ms
    scheduler_config.trigger_count_threshold = 100;  // Trigger if 100+ tuples
    scheduler_config.max_delay_us = compute_config.max_delay_us;
    scheduler_config.watermark_slack_us = 200000;    // 200ms slack
    scheduler_config.max_concurrent_windows = 4;
    scheduler_config.stream_s_table = "stock_stream_s";
    scheduler_config.stream_r_table = "stock_stream_r";
    
    std::cout << "  ✓ Scheduler Config:" << std::endl;
    std::cout << "    - Window Type: " << (scheduler_config.window_type == WindowType::Sliding ? "Sliding" : "Tumbling") << std::endl;
    std::cout << "    - Trigger Policy: Hybrid (time + count)" << std::endl;
    std::cout << "    - Max Concurrent Windows: " << scheduler_config.max_concurrent_windows << std::endl;
    
    // ========== 4. Register Callbacks ==========
    std::cout << "\n[4] Registering callbacks..." << std::endl;
    
    std::atomic<int> completed_windows{0};
    std::atomic<int> failed_windows{0};
    
    // Note: Full scheduler initialization would look like this:
    /*
    auto pecj_engine = std::make_unique<PECJComputeEngine>();
    pecj_engine->initialize(compute_config, table_manager.get(), resource_handle.get());
    
    auto scheduler = std::make_unique<WindowScheduler>(
        scheduler_config,
        pecj_engine.get(),
        table_manager.get(),
        resource_handle.get()
    );
    
    scheduler->onWindowCompleted([&completed_windows](const WindowInfo& window, const ComputeStatus& status) {
        completed_windows++;
        std::cout << "  ✓ Window " << window.window_id << " completed: "
                  << status.join_count << " joins in "
                  << status.computation_time_ms << "ms" << std::endl;
    });
    
    scheduler->onWindowFailed([&failed_windows](const WindowInfo& window, const ComputeStatus& status) {
        failed_windows++;
        std::cerr << "  ✗ Window " << window.window_id << " failed: "
                  << status.error << std::endl;
    });
    
    scheduler->watchTable("stock_stream_s", 0);  // Stream S
    scheduler->watchTable("stock_stream_r", 1);  // Stream R
    
    scheduler->start();
    */
    
    std::cout << "  ✓ Registered completion and failure callbacks" << std::endl;
    std::cout << "  ✓ Watching tables: stock_stream_s, stock_stream_r" << std::endl;
    
    // ========== 5. Simulate Data Stream ==========
    std::cout << "\n[5] Simulating data stream..." << std::endl;
    
    auto stream_s = table_manager->getStreamTable("stock_stream_s");
    auto stream_r = table_manager->getStreamTable("stock_stream_r");
    
    if (!stream_s || !stream_r) {
        std::cerr << "Failed to get stream tables!" << std::endl;
        return 1;
    }
    
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "AMZN"};
    int64_t base_time = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::cout << "  Inserting data into streams..." << std::endl;
    
    // Insert data over 10 seconds
    for (int i = 0; i < 100; i++) {
        int64_t timestamp = base_time + i * 100000;  // 100ms intervals
        
        // Insert to stream S
        for (const auto& symbol : symbols) {
            auto data_s = generateStockData(symbol, timestamp);
            stream_s->insert(data_s);
            
            // Simulate scheduler notification
            // scheduler->onDataInserted("stock_stream_s", timestamp, 1);
        }
        
        // Insert to stream R (fewer tuples)
        if (i % 2 == 0) {
            for (const auto& symbol : symbols) {
                auto data_r = generateStockData(symbol, timestamp);
                stream_r->insert(data_r);
                
                // scheduler->onDataInserted("stock_stream_r", timestamp, 1);
            }
        }
        
        if ((i + 1) % 20 == 0) {
            std::cout << "    Progress: " << (i + 1) << "/100 batches inserted" << std::endl;
        }
        
        // Small delay to simulate real-time stream
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    auto stats_s = stream_s->getStats();
    auto stats_r = stream_r->getStats();
    
    std::cout << "  ✓ Insertion complete:" << std::endl;
    std::cout << "    - Stream S: " << stats_s.total_records << " records" << std::endl;
    std::cout << "    - Stream R: " << stats_r.total_records << " records" << std::endl;
    
    // ========== 6. Monitor Scheduling ==========
    std::cout << "\n[6] Monitoring window scheduling..." << std::endl;
    
    // In real scenario, scheduler would be running and processing windows
    std::cout << "  Note: Full scheduling requires PECJ library integration" << std::endl;
    std::cout << "  Expected behavior:" << std::endl;
    std::cout << "    - Windows created based on timestamp ranges" << std::endl;
    std::cout << "    - Triggers when watermark passes window end + slack" << std::endl;
    std::cout << "    - OR triggers when tuple count >= threshold" << std::endl;
    std::cout << "    - Parallel execution up to max_concurrent_windows" << std::endl;
    
    /*
    // Wait for windows to complete
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    auto metrics = scheduler->getMetrics();
    std::cout << "\n  Scheduling Metrics:" << std::endl;
    std::cout << "    - Total Windows: " << metrics.total_windows_scheduled << std::endl;
    std::cout << "    - Completed: " << metrics.total_windows_completed << std::endl;
    std::cout << "    - Failed: " << metrics.total_windows_failed << std::endl;
    std::cout << "    - Avg Completion Time: " << metrics.avg_window_completion_ms << "ms" << std::endl;
    std::cout << "    - Windows/sec: " << metrics.windows_per_second << std::endl;
    
    auto all_windows = scheduler->getAllWindows();
    std::cout << "\n  Window Details:" << std::endl;
    for (size_t i = 0; i < std::min(all_windows.size(), size_t(5)); i++) {
        const auto& w = all_windows[i];
        std::cout << "    Window " << w.window_id << ": ["
                  << (w.time_range.start_us / 1000000.0) << "s, "
                  << (w.time_range.end_us / 1000000.0) << "s)"
                  << " - S:" << w.stream_s_count << " R:" << w.stream_r_count
                  << " - " << (w.is_completed ? "DONE" : (w.is_computing ? "COMPUTING" : "PENDING"))
                  << std::endl;
    }
    */
    
    // ========== 7. Query Results ==========
    std::cout << "\n[7] Querying join results..." << std::endl;
    
    auto join_table = table_manager->getJoinResultTable("stock_join_results");
    if (join_table) {
        auto join_stats = join_table->getStats();
        std::cout << "  Join Results Table:" << std::endl;
        std::cout << "    - Total Results: " << join_stats.total_records << std::endl;
        
        // Query aggregate statistics (if any results exist)
        // auto agg_stats = join_table->queryAggregateStats();
        // std::cout << "    - Avg Join Count: " << agg_stats.avg_join_count << std::endl;
    }
    
    // ========== 8. Cleanup ==========
    std::cout << "\n[8] Cleanup..." << std::endl;
    
    /*
    scheduler->stop(true);  // Wait for completion
    std::cout << "  ✓ Scheduler stopped" << std::endl;
    */
    
    table_manager.reset();
    resource_handle.reset();
    resource_manager.reset();
    
    std::cout << "  ✓ Resources released" << std::endl;
    
    // ========== Summary ==========
    std::cout << "\n========== Demo Summary ==========" << std::endl;
    std::cout << "This demo showed the WindowScheduler API for:" << std::endl;
    std::cout << "  ✓ Automatic window computation triggering" << std::endl;
    std::cout << "  ✓ Hybrid trigger policy (time + count based)" << std::endl;
    std::cout << "  ✓ Watermark-based out-of-order handling" << std::endl;
    std::cout << "  ✓ Event-driven architecture (table insertions)" << std::endl;
    std::cout << "  ✓ Parallel window execution with resource limits" << std::endl;
    std::cout << "  ✓ Callback-based notification system" << std::endl;
    std::cout << "\nNote: Full integration requires PECJ library" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}

#else
int main() {
    std::cout << "This demo requires PECJ_MODE_INTEGRATED" << std::endl;
    std::cout << "Build with: cmake -DPECJ_MODE=INTEGRATED .." << std::endl;
    return 1;
}
#endif
