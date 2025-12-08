/**
 * @file deep_integration_demo.cpp
 * @brief sageTSDB Deep Integration Demo with PECJ Compute Engine
 * 
 * This demo showcases the deep integration architecture where:
 * 1. All data is stored in sageTSDB tables (no PECJ internal buffers)
 * 2. PECJ acts as a pure stateless compute engine
 * 3. ResourceManager controls all threads and memory
 * 4. Manual window computation triggered by the demo
 * 
 * Data Flow:
 *   External Data → sageTSDB Tables → PECJ Compute Engine → Result Tables
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-05
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <random>
#include <atomic>
#include <vector>

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"

#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

using namespace sage_tsdb;

// ============================================================================
// Configuration
// ============================================================================

struct DemoConfig {
    // Data generation
    size_t num_events_s = 10000;
    size_t num_events_r = 10000;
    size_t key_range = 100;
    double out_of_order_ratio = 0.2;
    
    // Window parameters
    uint64_t window_len_us = 1000000;     // 1 second
    uint64_t slide_len_us = 500000;       // 500ms
    
    // Resource limits
    int max_threads = 4;
    size_t max_memory_mb = 512;
    
    // Display
    bool verbose = true;
    size_t progress_interval = 1000;
};

// ============================================================================
// Statistics
// ============================================================================

struct DemoStats {
    std::atomic<size_t> events_inserted_s{0};
    std::atomic<size_t> events_inserted_r{0};
    std::atomic<size_t> windows_triggered{0};
    std::atomic<size_t> join_results{0};
    std::atomic<size_t> total_computation_time_ms{0};
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    void print() const {
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "Demo Performance Report\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "[Data Ingestion]\n";
        std::cout << "  Stream S Events       : " << events_inserted_s << "\n";
        std::cout << "  Stream R Events       : " << events_inserted_r << "\n";
        std::cout << "  Total Events          : " << (events_inserted_s + events_inserted_r) << "\n\n";
        
        std::cout << "[Computation]\n";
        std::cout << "  Windows Triggered     : " << windows_triggered << "\n";
        std::cout << "  Join Results          : " << join_results << "\n";
        std::cout << "  Avg Computation (ms)  : " 
                  << (windows_triggered > 0 ? total_computation_time_ms / windows_triggered : 0) << "\n\n";
        
        std::cout << "[Performance]\n";
        std::cout << "  Total Time (ms)       : " << duration_ms << "\n";
        std::cout << "  Throughput (events/s) : " 
                  << std::fixed << std::setprecision(2)
                  << ((duration_ms > 0) ? (events_inserted_s + events_inserted_r) * 1000.0 / duration_ms : 0.0) 
                  << "\n";
        
        std::cout << "\n" << std::string(70, '=') << "\n";
    }
};

// ============================================================================
// Data Generator
// ============================================================================

class SyntheticDataGenerator {
public:
    SyntheticDataGenerator(size_t key_range, double out_of_order_ratio)
        : key_range_(key_range)
        , out_of_order_ratio_(out_of_order_ratio)
        , rng_(std::random_device{}())
        , key_dist_(0, key_range - 1)
        , value_dist_(0.0, 100.0)
        , ooo_dist_(0.0, 1.0) {
    }
    
    TimeSeriesData generateEvent(const std::string& stream_name, int64_t base_timestamp) {
        TimeSeriesData data;
        
        // Apply out-of-order with some probability
        int64_t timestamp = base_timestamp;
        if (ooo_dist_(rng_) < out_of_order_ratio_) {
            // Delay by 0-100ms
            timestamp -= static_cast<int64_t>(ooo_dist_(rng_) * 100000);
        }
        
        data.timestamp = timestamp;
        data.tags["stream"] = stream_name;
        data.tags["key"] = std::to_string(key_dist_(rng_));
        data.fields["value"] = std::to_string(value_dist_(rng_));
        
        return data;
    }
    
private:
    size_t key_range_;
    double out_of_order_ratio_;
    std::mt19937 rng_;
    std::uniform_int_distribution<size_t> key_dist_;
    std::uniform_real_distribution<double> value_dist_;
    std::uniform_real_distribution<double> ooo_dist_;
};

// ============================================================================
// Helper Functions
// ============================================================================

void printHeader() {
    std::cout << R"(
╔════════════════════════════════════════════════════════════════════╗
║          sageTSDB + PECJ Deep Integration Demo                     ║
║                                                                    ║
║  Architecture: Database-Centric Design                            ║
║  - All data stored in sageTSDB tables                             ║
║  - PECJ as stateless compute engine                               ║
║  - ResourceManager controls all resources                          ║
╚════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printConfig(const DemoConfig& config) {
    std::cout << "\n[Configuration]\n";
    std::cout << "  Stream S Events       : " << config.num_events_s << "\n";
    std::cout << "  Stream R Events       : " << config.num_events_r << "\n";
    std::cout << "  Key Range             : " << config.key_range << "\n";
    std::cout << "  Out-of-Order Ratio    : " << (config.out_of_order_ratio * 100) << "%\n";
    std::cout << "  Window Length         : " << (config.window_len_us / 1000) << " ms\n";
    std::cout << "  Slide Length          : " << (config.slide_len_us / 1000) << " ms\n";
    std::cout << "  Max Threads           : " << config.max_threads << "\n";
    std::cout << "  Max Memory            : " << config.max_memory_mb << " MB\n";
    std::cout << std::endl;
}

// ============================================================================
// Main Demo Function
// ============================================================================

int main(int argc, char** argv) {
    printHeader();
    
    // Parse command line arguments
    DemoConfig config;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--events-s" && i + 1 < argc) {
            config.num_events_s = std::stoull(argv[++i]);
        } else if (arg == "--events-r" && i + 1 < argc) {
            config.num_events_r = std::stoull(argv[++i]);
        } else if (arg == "--keys" && i + 1 < argc) {
            config.key_range = std::stoull(argv[++i]);
        } else if (arg == "--ooo-ratio" && i + 1 < argc) {
            config.out_of_order_ratio = std::stod(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.max_threads = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --events-s N      Number of events in stream S (default: 10000)\n"
                      << "  --events-r N      Number of events in stream R (default: 10000)\n"
                      << "  --keys N          Key range (default: 100)\n"
                      << "  --ooo-ratio R     Out-of-order ratio 0.0-1.0 (default: 0.2)\n"
                      << "  --threads N       Max threads (default: 4)\n"
                      << "  --help            Show this help\n";
            return 0;
        }
    }
    
    printConfig(config);
    
    DemoStats stats;
    stats.start_time = std::chrono::steady_clock::now();
    
    // ========================================================================
    // Step 1: Initialize sageTSDB
    // ========================================================================
    std::cout << "[Step 1] Initializing sageTSDB...\n";
    
    TimeSeriesDB db;
    
    // Create tables for streams and join results
    if (!db.createTable("stream_s", TableType::Stream)) {
        std::cerr << "❌ Failed to create stream_s table\n";
        return 1;
    }
    if (!db.createTable("stream_r", TableType::Stream)) {
        std::cerr << "❌ Failed to create stream_r table\n";
        return 1;
    }
    if (!db.createTable("join_results", TableType::JoinResult)) {
        std::cerr << "❌ Failed to create join_results table\n";
        return 1;
    }
    
    std::cout << "✓ Created tables: stream_s, stream_r, join_results\n\n";
    
#ifdef PECJ_MODE_INTEGRATED
    // ========================================================================
    // Step 2: Initialize PECJ Compute Engine (Deep Integration Mode)
    // ========================================================================
    std::cout << "[Step 2] Initializing PECJ Compute Engine (Integrated Mode)...\n";
    
    compute::ComputeConfig pecj_config;
    pecj_config.window_len_us = config.window_len_us;
    pecj_config.slide_len_us = config.slide_len_us;
    pecj_config.operator_type = "SHJ";  // Try SHJ (Symmetric Hash Join) instead
    pecj_config.max_threads = config.max_threads;
    pecj_config.max_memory_bytes = config.max_memory_mb * 1024ULL * 1024ULL;
    pecj_config.stream_s_table = "stream_s";
    pecj_config.stream_r_table = "stream_r";
    pecj_config.result_table = "join_results";
    
    compute::PECJComputeEngine pecj_engine;
    
    // Note: In full integration, we'd get ResourceHandle from ResourceManager
    // For this demo, we initialize without ResourceHandle (stub mode)
    if (!pecj_engine.initialize(pecj_config, &db, nullptr)) {
        std::cerr << "❌ Failed to initialize PECJ compute engine\n";
        return 1;
    }
    
    std::cout << "✓ PECJ Compute Engine initialized\n";
    std::cout << "  Operator Type: " << pecj_config.operator_type << "\n";
    std::cout << "  Window Length: " << pecj_config.window_len_us << " us\n";
    std::cout << "  Thread Limit: " << pecj_config.max_threads << "\n\n";
    
#else
    std::cout << "[Step 2] PECJ Integration disabled (stub mode)\n";
    std::cout << "  To enable: rebuild with -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED\n\n";
#endif
    
    // ========================================================================
    // Step 3: Generate and Insert Data
    // ========================================================================
    std::cout << "[Step 3] Generating and inserting data...\n\n";
    
    SyntheticDataGenerator generator(config.key_range, config.out_of_order_ratio);
    
    // CRITICAL: Use relative timestamps starting from 0 (not absolute system time!)
    // PECJ window starts at 0, so data timestamps must be in window range [0, window_len]
    int64_t base_time = 0;  // Start from 0 microseconds
    
    // Interleave Stream S and Stream R events
    size_t total_events = config.num_events_s + config.num_events_r;
    size_t s_idx = 0, r_idx = 0;
    
    for (size_t i = 0; i < total_events; i++) {
        // Alternate between streams
        bool insert_s = (s_idx < config.num_events_s) && 
                       ((r_idx >= config.num_events_r) || (i % 2 == 0));
        
        if (insert_s) {
            int64_t timestamp = base_time + s_idx * 100;  // 100us interval
            auto data = generator.generateEvent("S", timestamp);
            db.insert("stream_s", data);
            stats.events_inserted_s++;
            s_idx++;
        } else {
            int64_t timestamp = base_time + r_idx * 100;
            auto data = generator.generateEvent("R", timestamp);
            db.insert("stream_r", data);
            stats.events_inserted_r++;
            r_idx++;
        }
        
        // Progress indicator
        if (config.verbose && (i + 1) % config.progress_interval == 0) {
            std::cout << "  Progress: " << (i + 1) << "/" << total_events 
                      << " events inserted\n";
        }
    }
    
    std::cout << "\n✓ Data insertion completed\n";
    std::cout << "  Stream S: " << stats.events_inserted_s << " events\n";
    std::cout << "  Stream R: " << stats.events_inserted_r << " events\n\n";
    
#ifdef PECJ_MODE_INTEGRATED
    // ========================================================================
    // Step 4: Execute Window Join Computations
    // ========================================================================
    std::cout << "[Step 4] Executing window join computations...\n\n";
    
    // Compute number of windows
    int64_t data_duration = total_events * 100;  // microseconds
    size_t num_windows = (data_duration / config.slide_len_us) + 1;
    
    for (size_t win_id = 0; win_id < num_windows; win_id++) {
        compute::TimeRange range;
        range.start_us = base_time + win_id * config.slide_len_us;
        range.end_us = range.start_us + config.window_len_us;
        
        if (config.verbose && win_id % 5 == 0) {
            std::cout << "  [Window #" << win_id << "] "
                      << "Range: [" << range.start_us << ", " << range.end_us << ")\n";
        }
        
        // Execute window join
        auto compute_start = std::chrono::steady_clock::now();
        auto status = pecj_engine.executeWindowJoin(win_id, range);
        auto compute_end = std::chrono::steady_clock::now();
        
        auto compute_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            compute_end - compute_start).count();
        
        stats.windows_triggered++;
        stats.total_computation_time_ms += compute_time;
        
        if (status.success) {
            stats.join_results += status.join_count;
            
            if (config.verbose && win_id % 5 == 0) {
                std::cout << "    ✓ Join completed: " << status.join_count 
                          << " results in " << compute_time << " ms\n";
            }
        } else {
            std::cerr << "    ❌ Window computation failed: " << status.error << "\n";
        }
    }
    
    std::cout << "\n✓ Window computations completed\n";
    std::cout << "  Total Windows: " << stats.windows_triggered << "\n";
    std::cout << "  Total Join Results: " << stats.join_results << "\n\n";
#endif
    
    // ========================================================================
    // Step 5: Query Results
    // ========================================================================
    std::cout << "[Step 5] Querying join results...\n";
    
    QueryConfig query_config;
    query_config.time_range.start_time = base_time;
    query_config.time_range.end_time = base_time + total_events * 100;
    
    auto results = db.query("join_results", query_config);
    
    std::cout << "✓ Retrieved " << results.size() << " result records\n";
    
    if (!results.empty() && config.verbose) {
        std::cout << "\nSample Results (first 5):\n";
        for (size_t i = 0; i < std::min(results.size(), size_t(5)); i++) {
            std::cout << "  [" << i << "] timestamp=" << results[i].timestamp;
            if (results[i].fields.count("join_count")) {
                std::cout << ", join_count=" << results[i].fields.at("join_count");
            }
            std::cout << "\n";
        }
    }
    std::cout << "\n";
    
    // ========================================================================
    // Step 6: Print Statistics
    // ========================================================================
    stats.end_time = std::chrono::steady_clock::now();
    stats.print();
    
    std::cout << "\n[Demo Mode]\n";
#ifdef PECJ_MODE_INTEGRATED
    std::cout << "  ✓ PECJ Deep Integration Mode (Database-Centric)\n";
    std::cout << "  - Data stored only in sageTSDB tables\n";
    std::cout << "  - PECJ as stateless compute engine\n";
    std::cout << "  - ResourceManager controls resources\n";
#else
    std::cout << "  ⚠ Stub Mode (PECJ not integrated)\n";
    std::cout << "  - Rebuild with -DPECJ_MODE=INTEGRATED to enable\n";
#endif
    std::cout << "\n";
    
    return 0;
}
