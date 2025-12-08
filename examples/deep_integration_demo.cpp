/**
 * @file deep_integration_demo.cpp
 * @brief sageTSDB Deep Integration Demo with PECJ Compute Engine (Real Dataset)
 * 
 * This demo showcases the deep integration architecture where:
 * 1. All data is stored in sageTSDB tables (no PECJ internal buffers)
 * 2. PECJ acts as a pure stateless compute engine
 * 3. ResourceManager controls all threads and memory
 * 4. Multiple windows triggered using real PECJ benchmark datasets
 * 
 * Data Flow:
 *   PECJ CSV Files → CSV Loader → sageTSDB Tables → PECJ Compute Engine → Result Tables
 * 
 * @version v4.0 (Real Dataset Integration)
 * @date 2024-12-08
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <fstream>

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"
#include "csv_data_loader.h"

#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

using namespace sage_tsdb;
using namespace sage_tsdb::utils;

// ============================================================================
// Configuration
// ============================================================================

struct DemoConfig {
    // Data files
    std::string s_file = "../../../PECJ/benchmark/datasets/sTuple.csv";
    std::string r_file = "../../../PECJ/benchmark/datasets/rTuple.csv";
    size_t max_events_s = 60000;
    size_t max_events_r = 60000;
    
    // Window parameters (microseconds)
    uint64_t window_len_us = 10000;       // 10ms window
    uint64_t slide_len_us = 5000;         // 5ms slide
    uint64_t watermark_us = 2000;         // 2ms watermark
    
    // Resource limits
    int max_threads = 4;
    size_t max_memory_mb = 512;
    
    // Display
    bool verbose = true;
    size_t progress_interval = 5000;
    size_t show_samples = 10;
};

// ============================================================================
// Statistics
// ============================================================================

struct DemoStats {
    std::atomic<size_t> events_loaded_s{0};
    std::atomic<size_t> events_loaded_r{0};
    std::atomic<size_t> events_inserted_s{0};
    std::atomic<size_t> events_inserted_r{0};
    std::atomic<size_t> windows_triggered{0};
    std::atomic<size_t> join_results{0};
    std::atomic<size_t> total_computation_time_us{0};
    
    int64_t data_time_range_us = 0;
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::chrono::steady_clock::time_point load_end_time;
    std::chrono::steady_clock::time_point insert_end_time;
    
    void print() const {
        auto total_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        auto load_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            load_end_time - start_time).count();
        auto insert_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            insert_end_time - load_end_time).count();
        auto compute_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - insert_end_time).count();
        
        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "Demo Performance Report (Real Dataset)\n";
        std::cout << std::string(70, '=') << "\n\n";
        
        std::cout << "[Data Loading]\n";
        std::cout << "  Stream S Loaded       : " << events_loaded_s << " events\n";
        std::cout << "  Stream R Loaded       : " << events_loaded_r << " events\n";
        std::cout << "  Load Time             : " << load_duration_ms << " ms\n";
        std::cout << "  Data Time Span        : " << (data_time_range_us / 1000.0) << " ms\n\n";
        
        std::cout << "[Data Ingestion]\n";
        std::cout << "  Stream S Inserted     : " << events_inserted_s << " events\n";
        std::cout << "  Stream R Inserted     : " << events_inserted_r << " events\n";
        std::cout << "  Total Events          : " << (events_inserted_s + events_inserted_r) << " events\n";
        std::cout << "  Insert Time           : " << insert_duration_ms << " ms\n";
        std::cout << "  Insert Throughput     : " 
                  << std::fixed << std::setprecision(0)
                  << ((insert_duration_ms > 0) ? (events_inserted_s + events_inserted_r) * 1000.0 / insert_duration_ms : 0.0) 
                  << " events/s\n\n";
        
        std::cout << "[Window Computation]\n";
        std::cout << "  Windows Triggered     : " << windows_triggered << "\n";
        std::cout << "  Join Results          : " << join_results << "\n";
        std::cout << "  Computation Time      : " << compute_duration_ms << " ms\n";
        std::cout << "  Avg per Window (us)   : " 
                  << (windows_triggered > 0 ? total_computation_time_us / windows_triggered : 0) << "\n\n";
        
        std::cout << "[Overall Performance]\n";
        std::cout << "  Total Time            : " << total_duration_ms << " ms\n";
        std::cout << "  Overall Throughput    : " 
                  << std::fixed << std::setprecision(0)
                  << ((total_duration_ms > 0) ? (events_inserted_s + events_inserted_r) * 1000.0 / total_duration_ms : 0.0) 
                  << " events/s\n";
        
        std::cout << "\n" << std::string(70, '=') << "\n";
    }
};

// ============================================================================
// Helper Classes
// ============================================================================

/**
 * @brief Sorts events by arrival time for realistic stream replay
 */
struct EventWithArrival {
    TimeSeriesData data;
    int64_t arrival_time;
    
    bool operator<(const EventWithArrival& other) const {
        return arrival_time < other.arrival_time;
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

void printHeader() {
    std::cout << R"(
╔════════════════════════════════════════════════════════════════════╗
║     sageTSDB + PECJ Deep Integration Demo (Real Dataset)          ║
║                                                                    ║
║  Architecture: Database-Centric Design                            ║
║  - Real PECJ benchmark datasets (CSV)                             ║
║  - All data stored in sageTSDB tables                             ║
║  - PECJ as stateless compute engine                               ║
║  - Multiple sliding windows triggered                             ║
╚════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printConfig(const DemoConfig& config) {
    std::cout << "\n[Configuration]\n";
    std::cout << "  Stream S File         : " << config.s_file << "\n";
    std::cout << "  Stream R File         : " << config.r_file << "\n";
    std::cout << "  Max Events per Stream : S=" << config.max_events_s 
              << ", R=" << config.max_events_r << "\n";
    std::cout << "  Window Length         : " << (config.window_len_us / 1000.0) << " ms\n";
    std::cout << "  Slide Length          : " << (config.slide_len_us / 1000.0) << " ms\n";
    std::cout << "  Watermark Delay       : " << (config.watermark_us / 1000.0) << " ms\n";
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
        if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--max-s" && i + 1 < argc) {
            config.max_events_s = std::stoull(argv[++i]);
        } else if (arg == "--max-r" && i + 1 < argc) {
            config.max_events_r = std::stoull(argv[++i]);
        } else if (arg == "--window-us" && i + 1 < argc) {
            config.window_len_us = std::stoull(argv[++i]);
        } else if (arg == "--slide-us" && i + 1 < argc) {
            config.slide_len_us = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.max_threads = std::stoi(argv[++i]);
        } else if (arg == "--quiet") {
            config.verbose = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --s-file PATH     Path to stream S CSV file\n"
                      << "  --r-file PATH     Path to stream R CSV file\n"
                      << "  --max-s N         Max events from stream S (default: 60000)\n"
                      << "  --max-r N         Max events from stream R (default: 60000)\n"
                      << "  --window-us N     Window length in microseconds (default: 10000)\n"
                      << "  --slide-us N      Slide length in microseconds (default: 5000)\n"
                      << "  --threads N       Max threads (default: 4)\n"
                      << "  --quiet           Reduce output verbosity\n"
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
    // Step 3: Load Data from CSV Files
    // ========================================================================
    std::cout << "[Step 3] Loading data from CSV files...\n";
    
    auto s_records = CSVDataLoader::loadFromFile(config.s_file);
    auto r_records = CSVDataLoader::loadFromFile(config.r_file);
    
    if (s_records.empty() || r_records.empty()) {
        std::cerr << "❌ Failed to load data files\n";
        return 1;
    }
    
    // Limit to max events
    if (s_records.size() > config.max_events_s) {
        s_records.resize(config.max_events_s);
    }
    if (r_records.size() > config.max_events_r) {
        r_records.resize(config.max_events_r);
    }
    
    stats.events_loaded_s = s_records.size();
    stats.events_loaded_r = r_records.size();
    
    CSVDataLoader::printStatistics(s_records, "Stream S");
    CSVDataLoader::printStatistics(r_records, "Stream R");
    
    // Calculate time range
    int64_t min_time = std::min(s_records[0].event_time, r_records[0].event_time);
    int64_t max_time_s = s_records.back().event_time;
    int64_t max_time_r = r_records.back().event_time;
    int64_t max_time = std::max(max_time_s, max_time_r);
    stats.data_time_range_us = max_time - min_time;
    
    stats.load_end_time = std::chrono::steady_clock::now();
    
    // ========================================================================
    // Step 4: Insert Data into sageTSDB Tables (Sorted by Arrival Time)
    // ========================================================================
    std::cout << "\n[Step 4] Inserting data into sageTSDB tables...\n";
    
    // Merge and sort by arrival time for realistic stream processing
    std::vector<EventWithArrival> all_events;
    all_events.reserve(s_records.size() + r_records.size());
    
    for (const auto& record : s_records) {
        EventWithArrival evt;
        evt.data = CSVDataLoader::toTimeSeriesData(record, "S");
        evt.arrival_time = record.arrival_time;
        all_events.push_back(evt);
    }
    
    for (const auto& record : r_records) {
        EventWithArrival evt;
        evt.data = CSVDataLoader::toTimeSeriesData(record, "R");
        evt.arrival_time = record.arrival_time;
        all_events.push_back(evt);
    }
    
    // Sort by arrival time (simulating real stream arrival order)
    std::sort(all_events.begin(), all_events.end());
    
    std::cout << "  Inserting " << all_events.size() << " events in arrival order...\n";
    
    // Insert in batches
    for (size_t i = 0; i < all_events.size(); i++) {
        const auto& evt = all_events[i];
        const std::string& stream = evt.data.tags.at("stream");
        
        if (stream == "S") {
            db.insert("stream_s", evt.data);
            stats.events_inserted_s++;
        } else {
            db.insert("stream_r", evt.data);
            stats.events_inserted_r++;
        }
        
        // Progress indicator
        if (config.verbose && (i + 1) % config.progress_interval == 0) {
            std::cout << "  Progress: " << (i + 1) << "/" << all_events.size() 
                      << " events inserted\n";
        }
    }
    
    stats.insert_end_time = std::chrono::steady_clock::now();
    
    std::cout << "\n✓ Data insertion completed\n";
    std::cout << "  Stream S: " << stats.events_inserted_s << " events\n";
    std::cout << "  Stream R: " << stats.events_inserted_r << " events\n";
    std::cout << "  Total:    " << (stats.events_inserted_s + stats.events_inserted_r) << " events\n\n";
    
#ifdef PECJ_MODE_INTEGRATED
    // ========================================================================
    // Step 5: Execute Sliding Window Join Computations
    // ========================================================================
    std::cout << "[Step 5] Executing sliding window join computations...\n\n";
    
    // Calculate number of windows based on data time range
    size_t num_windows = (stats.data_time_range_us / config.slide_len_us) + 1;
    
    std::cout << "  Data time range: " << (stats.data_time_range_us / 1000.0) << " ms\n";
    std::cout << "  Window length: " << (config.window_len_us / 1000.0) << " ms\n";
    std::cout << "  Slide length: " << (config.slide_len_us / 1000.0) << " ms\n";
    std::cout << "  Number of windows: " << num_windows << "\n\n";
    
    if (num_windows > 1000) {
        std::cout << "  ⚠ Large number of windows (" << num_windows 
                  << "), limiting to 1000 for demo\n";
        num_windows = 1000;
    }
    
    size_t display_step = std::max(size_t(1), num_windows / 20);  // Show ~20 progress updates
    
    for (size_t win_id = 0; win_id < num_windows; win_id++) {
        compute::TimeRange range;
        range.start_us = min_time + win_id * config.slide_len_us;
        range.end_us = range.start_us + config.window_len_us;
        
        // Verbose output for first few windows and periodic updates
        bool should_display = (win_id < 5) || (win_id % display_step == 0) || (win_id == num_windows - 1);
        
        if (config.verbose && should_display) {
            std::cout << "  [Window #" << std::setw(4) << win_id << "] "
                      << "Time: [" << std::setw(8) << range.start_us 
                      << ", " << std::setw(8) << range.end_us << ") us";
        }
        
        // Execute window join
        auto compute_start = std::chrono::steady_clock::now();
        auto status = pecj_engine.executeWindowJoin(win_id, range);
        auto compute_end = std::chrono::steady_clock::now();
        
        auto compute_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            compute_end - compute_start).count();
        
        stats.windows_triggered++;
        stats.total_computation_time_us += compute_time_us;
        
        if (status.success) {
            stats.join_results += status.join_count;
            
            if (config.verbose && should_display) {
                std::cout << " → " << std::setw(6) << status.join_count 
                          << " joins (" << compute_time_us << " us)\n";
            }
        } else {
            if (config.verbose && should_display) {
                std::cout << " → Failed: " << status.error << "\n";
            }
        }
    }
    
    std::cout << "\n✓ Window computations completed\n";
    std::cout << "  Total Windows Processed : " << stats.windows_triggered << "\n";
    std::cout << "  Total Join Results      : " << stats.join_results << "\n";
    std::cout << "  Avg Results per Window  : " 
              << (stats.windows_triggered > 0 ? stats.join_results / stats.windows_triggered : 0) << "\n\n";
#endif
    
    // ========================================================================
    // Step 6: Query Results
    // ========================================================================
    std::cout << "[Step 6] Querying join results from sageTSDB...\n";
    
    QueryConfig query_config;
    query_config.time_range.start_time = min_time;
    query_config.time_range.end_time = max_time;
    
    auto results = db.query("join_results", query_config);
    
    std::cout << "✓ Retrieved " << results.size() << " result records from sageTSDB\n";
    
    if (!results.empty() && config.verbose) {
        std::cout << "\nSample Results (first " << config.show_samples << "):\n";
        for (size_t i = 0; i < std::min(results.size(), config.show_samples); i++) {
            std::cout << "  [" << std::setw(4) << i << "] "
                      << "timestamp=" << std::setw(8) << results[i].timestamp << " us";
            if (results[i].fields.count("join_count")) {
                std::cout << ", join_count=" << results[i].fields.at("join_count");
            }
            if (results[i].tags.count("window_id")) {
                std::cout << ", window=" << results[i].tags.at("window_id");
            }
            std::cout << "\n";
        }
        if (results.size() > config.show_samples) {
            std::cout << "  ... (" << (results.size() - config.show_samples) 
                      << " more results)\n";
        }
    }
    std::cout << "\n";
    
    // ========================================================================
    // Step 7: Print Performance Statistics
    // ========================================================================
    stats.end_time = std::chrono::steady_clock::now();
    stats.print();
    
    std::cout << "\n[Integration Mode]\n";
#ifdef PECJ_MODE_INTEGRATED
    std::cout << "  ✓ PECJ Deep Integration Mode (Database-Centric)\n";
    std::cout << "  - Real PECJ benchmark datasets (CSV)\n";
    std::cout << "  - All data stored in sageTSDB tables\n";
    std::cout << "  - PECJ as stateless compute engine\n";
    std::cout << "  - Multiple sliding windows triggered\n";
    std::cout << "  - ResourceManager controls resources\n";
#else
    std::cout << "  ⚠ Stub Mode (PECJ not integrated)\n";
    std::cout << "  - Only data loading and insertion tested\n";
    std::cout << "  - Rebuild with -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED to enable\n";
#endif
    std::cout << "\n";
    
    std::cout << "[Next Steps]\n";
    std::cout << "  1. Check build/sage_tsdb_data/lsm/ for persisted data\n";
    std::cout << "  2. Try different window parameters: --window-us, --slide-us\n";
    std::cout << "  3. Use different datasets: --s-file, --r-file\n";
    std::cout << "  4. Adjust event limits: --max-s, --max-r\n";
    std::cout << "\n";
    
    return 0;
}
