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
#include <random>

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/utils/csv_data_loader.h"

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
    size_t max_events_s = 200000;          // Increased from 60000
    size_t max_events_r = 200000;          // Increased from 60000
    
    // Time unit conversion
    // Set to 1000 if CSV times are in milliseconds (ms -> us)
    // Set to 1 if CSV times are already in microseconds
    int64_t time_unit_multiplier = 1000;  // Default: assume CSV is in milliseconds
    
    // Window parameters (microseconds)
    uint64_t window_len_us = 10000;       // 10ms window
    uint64_t slide_len_us = 5000;         // 5ms slide
    uint64_t watermark_us = 2000;         // 2ms watermark
    
    // Out-of-order simulation
    bool enable_disorder = true;           // Enable out-of-order simulation
    double disorder_ratio = 0.3;           // 30% of events will be out-of-order
    int64_t max_disorder_us = 5000;        // Max disorder delay: 5ms
    
    // Resource limits
    int max_threads = 8;
    size_t max_memory_mb = 1024;           // Increased to 1GB
    
    // Display
    bool verbose = true;
    size_t progress_interval = 10000;      // Show progress every 10k events
    size_t show_samples = 10;
    bool show_disorder_stats = true;       // Show disorder statistics
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
    
    // Disorder statistics
    std::atomic<size_t> disordered_events{0};
    std::atomic<size_t> late_arrivals{0};          // Events arriving after watermark
    int64_t max_observed_disorder_us = 0;
    int64_t avg_disorder_us = 0;
    
    int64_t data_time_range_us = 0;
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    std::chrono::steady_clock::time_point load_end_time;
    std::chrono::steady_clock::time_point insert_end_time;
    std::chrono::steady_clock::time_point disorder_end_time;
    
    void print() const {
        auto total_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        auto load_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            load_end_time - start_time).count();
        auto disorder_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            disorder_end_time - load_end_time).count();
        auto insert_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            insert_end_time - disorder_end_time).count();
        auto compute_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - insert_end_time).count();
        
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Demo Performance Report - High Disorder & Large Scale\n";
        std::cout << std::string(80, '=') << "\n\n";
        
        std::cout << "[Data Loading]\n";
        std::cout << "  Stream S Loaded       : " << events_loaded_s << " events\n";
        std::cout << "  Stream R Loaded       : " << events_loaded_r << " events\n";
        std::cout << "  Total Loaded          : " << (events_loaded_s + events_loaded_r) << " events\n";
        std::cout << "  Load Time             : " << load_duration_ms << " ms\n";
        std::cout << "  Load Throughput       : " 
                  << std::fixed << std::setprecision(0)
                  << ((load_duration_ms > 0) ? (events_loaded_s + events_loaded_r) * 1000.0 / load_duration_ms : 0.0) 
                  << " events/s\n";
        std::cout << "  Data Time Span        : " << (data_time_range_us / 1000.0) << " ms\n\n";
        
        std::cout << "[Out-of-Order Simulation]\n";
        std::cout << "  Disordered Events     : " << disordered_events 
                  << " (" << std::fixed << std::setprecision(1) 
                  << (100.0 * disordered_events / (events_loaded_s + events_loaded_r)) << "%)\n";
        std::cout << "  Late Arrivals         : " << late_arrivals 
                  << " (events arriving after watermark)\n";
        std::cout << "  Max Disorder Delay    : " << (max_observed_disorder_us / 1000.0) << " ms\n";
        std::cout << "  Avg Disorder Delay    : " << (avg_disorder_us / 1000.0) << " ms\n";
        std::cout << "  Simulation Time       : " << disorder_duration_ms << " ms\n\n";
        
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
        std::cout << "  Avg Results/Window    : " 
                  << std::fixed << std::setprecision(2)
                  << (windows_triggered > 0 ? static_cast<double>(join_results) / windows_triggered : 0.0) << "\n";
        std::cout << "  Computation Time      : " << compute_duration_ms << " ms\n";
        std::cout << "  Avg per Window (us)   : " 
                  << (windows_triggered > 0 ? total_computation_time_us / windows_triggered : 0) << "\n";
        std::cout << "  Computation Throughput: " 
                  << std::fixed << std::setprecision(0)
                  << ((compute_duration_ms > 0) ? join_results * 1000.0 / compute_duration_ms : 0.0) 
                  << " joins/s\n\n";
        
        std::cout << "[Overall Performance]\n";
        std::cout << "  Total Time            : " << total_duration_ms << " ms\n";
        std::cout << "  Overall Throughput    : " 
                  << std::fixed << std::setprecision(0)
                  << ((total_duration_ms > 0) ? (events_inserted_s + events_inserted_r) * 1000.0 / total_duration_ms : 0.0) 
                  << " events/s\n";
        std::cout << "  End-to-End Latency    : " 
                  << std::fixed << std::setprecision(2)
                  << (total_duration_ms / 1000.0) << " seconds\n";
        
        std::cout << "\n" << std::string(80, '=') << "\n";
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
    int64_t event_time;       // Original event time for disorder calculation
    bool is_disordered;
    
    bool operator<(const EventWithArrival& other) const {
        return arrival_time < other.arrival_time;
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Apply out-of-order simulation to events
 */
void applyDisorder(std::vector<EventWithArrival>& events, 
                   const DemoConfig& config, 
                   DemoStats& stats) {
    if (!config.enable_disorder) {
        return;
    }
    
    std::cout << "\n[Disorder Simulation]\n";
    std::cout << "  Disorder Ratio        : " << (config.disorder_ratio * 100) << "%\n";
    std::cout << "  Max Disorder Delay    : " << (config.max_disorder_us / 1000.0) << " ms\n";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> disorder_prob(0.0, 1.0);
    std::uniform_int_distribution<int64_t> disorder_delay(0, config.max_disorder_us);
    
    int64_t total_disorder = 0;
    int64_t max_disorder = 0;
    
    for (auto& evt : events) {
        if (disorder_prob(gen) < config.disorder_ratio) {
            int64_t delay = disorder_delay(gen);
            evt.arrival_time += delay;
            evt.is_disordered = true;
            stats.disordered_events++;
            total_disorder += delay;
            max_disorder = std::max(max_disorder, delay);
            
            // Check if it's a late arrival (beyond watermark)
            if (delay > config.watermark_us) {
                stats.late_arrivals++;
            }
        }
    }
    
    stats.max_observed_disorder_us = max_disorder;
    if (stats.disordered_events > 0) {
        stats.avg_disorder_us = total_disorder / stats.disordered_events;
    }
    
    // Re-sort after applying disorder
    std::sort(events.begin(), events.end());
    
    std::cout << "  Applied to            : " << stats.disordered_events << " events\n";
    std::cout << "  Max Disorder Applied  : " << (max_disorder / 1000.0) << " ms\n";
    std::cout << "  Avg Disorder Applied  : " << (stats.avg_disorder_us / 1000.0) << " ms\n";
    std::cout << "  Late Arrivals         : " << stats.late_arrivals << " events\n";
    std::cout << "\n";
}

void printHeader() {
    std::cout << R"(
╔════════════════════════════════════════════════════════════════════╗
║   sageTSDB + PECJ: High Disorder & Large Scale Performance Demo   ║
║                                                                    ║
║  Test Scenario:                                                   ║
║  - Large-scale real-world datasets (100K+ events)                 ║
║  - High out-of-order arrival simulation (30% disorder)            ║
║  - Late event handling with watermark                             ║
║  - Multi-threaded sliding window joins                            ║
║                                                                    ║
║  Architecture: Database-Centric Design                            ║
║  - All data stored in sageTSDB tables                             ║
║  - PECJ as stateless compute engine                               ║
║  - ResourceManager controls threads & memory                      ║
╚════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printConfig(const DemoConfig& config) {
    std::cout << "\n[Configuration]\n";
    std::cout << "  Stream S File         : " << config.s_file << "\n";
    std::cout << "  Stream R File         : " << config.r_file << "\n";
    std::cout << "  Max Events per Stream : S=" << config.max_events_s 
              << ", R=" << config.max_events_r << "\n";
    std::cout << "  Total Scale           : ~" << (config.max_events_s + config.max_events_r) << " events\n";
    std::cout << "  CSV Time Unit         : " << (config.time_unit_multiplier == 1000 ? "milliseconds (ms)" : "microseconds (us)") << "\n";
    std::cout << "  Window Length         : " << (config.window_len_us / 1000.0) << " ms\n";
    std::cout << "  Slide Length          : " << (config.slide_len_us / 1000.0) << " ms\n";
    std::cout << "  Watermark Delay       : " << (config.watermark_us / 1000.0) << " ms\n";
    std::cout << "  Disorder Enabled      : " << (config.enable_disorder ? "YES" : "NO") << "\n";
    if (config.enable_disorder) {
        std::cout << "  Disorder Ratio        : " << (config.disorder_ratio * 100) << "%\n";
        std::cout << "  Max Disorder Delay    : " << (config.max_disorder_us / 1000.0) << " ms\n";
    }
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
        } else if (arg == "--time-unit" && i + 1 < argc) {
            std::string unit = argv[++i];
            if (unit == "ms" || unit == "milliseconds") {
                config.time_unit_multiplier = 1000;  // ms to us
            } else if (unit == "us" || unit == "microseconds") {
                config.time_unit_multiplier = 1;     // already us
            } else {
                std::cerr << "⚠ Unknown time unit: " << unit << " (use 'ms' or 'us')\n";
            }
        } else if (arg == "--disorder" && i + 1 < argc) {
            std::string val = argv[++i];
            config.enable_disorder = (val == "true" || val == "1" || val == "yes");
        } else if (arg == "--disorder-ratio" && i + 1 < argc) {
            config.disorder_ratio = std::stod(argv[++i]);
        } else if (arg == "--max-disorder-us" && i + 1 < argc) {
            config.max_disorder_us = std::stoll(argv[++i]);
        } else if (arg == "--quiet") {
            config.verbose = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  Data Source:\n"
                      << "    --s-file PATH         Path to stream S CSV file\n"
                      << "    --r-file PATH         Path to stream R CSV file\n"
                      << "    --max-s N             Max events from stream S (default: 200000)\n"
                      << "    --max-r N             Max events from stream R (default: 200000)\n"
                      << "    --time-unit UNIT      CSV time unit: 'ms' or 'us' (default: ms)\n"
                      << "\n"
                      << "  Window Parameters:\n"
                      << "    --window-us N         Window length in microseconds (default: 10000)\n"
                      << "    --slide-us N          Slide length in microseconds (default: 5000)\n"
                      << "\n"
                      << "  Disorder Simulation:\n"
                      << "    --disorder BOOL       Enable disorder (true/false, default: true)\n"
                      << "    --disorder-ratio R    Disorder ratio 0.0-1.0 (default: 0.3)\n"
                      << "    --max-disorder-us N   Max disorder delay in us (default: 5000)\n"
                      << "\n"
                      << "  Resources:\n"
                      << "    --threads N           Max threads (default: 8)\n"
                      << "\n"
                      << "  Display:\n"
                      << "    --quiet               Reduce output verbosity\n"
                      << "    --help                Show this help\n";
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
    std::cout << "  Time unit conversion: " 
              << (config.time_unit_multiplier == 1000 ? "ms → us (×1000)" : "us → us (×1)") << "\n\n";
    
    auto s_records = CSVDataLoader::loadFromFile(config.s_file, config.time_unit_multiplier);
    auto r_records = CSVDataLoader::loadFromFile(config.r_file, config.time_unit_multiplier);
    
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
    std::cout << "\n[Step 4] Preparing data stream with disorder simulation...\n";
    
    // Merge and sort by arrival time for realistic stream processing
    std::vector<EventWithArrival> all_events;
    all_events.reserve(s_records.size() + r_records.size());
    
    for (const auto& record : s_records) {
        EventWithArrival evt;
        evt.data = CSVDataLoader::toTimeSeriesData(record, "S");
        evt.arrival_time = record.arrival_time;
        evt.event_time = record.event_time;
        evt.is_disordered = false;
        all_events.push_back(evt);
    }
    
    for (const auto& record : r_records) {
        EventWithArrival evt;
        evt.data = CSVDataLoader::toTimeSeriesData(record, "R");
        evt.arrival_time = record.arrival_time;
        evt.event_time = record.event_time;
        evt.is_disordered = false;
        all_events.push_back(evt);
    }
    
    // Sort by arrival time (simulating real stream arrival order)
    std::sort(all_events.begin(), all_events.end());
    
    // Apply disorder simulation
    applyDisorder(all_events, config, stats);
    stats.disorder_end_time = std::chrono::steady_clock::now();
    
    std::cout << "[Step 4.1] Inserting data into sageTSDB tables...\n";
    std::cout << "  Total events to insert: " << all_events.size() << "\n";
    std::cout << "  Progress updates every: " << config.progress_interval << " events\n\n";
    
    auto insert_start = std::chrono::steady_clock::now();
    
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
        
        // Progress indicator with throughput
        if (config.verbose && (i + 1) % config.progress_interval == 0) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - insert_start).count();
            double throughput = (elapsed_ms > 0) ? (i + 1) * 1000.0 / elapsed_ms : 0.0;
            
            std::cout << "  Progress: " << std::setw(6) << (i + 1) << "/" << all_events.size() 
                      << " (" << std::fixed << std::setprecision(1) 
                      << (100.0 * (i + 1) / all_events.size()) << "%) - "
                      << std::setprecision(0) << throughput << " events/s\n";
        }
    }
    
    stats.insert_end_time = std::chrono::steady_clock::now();
    
    auto insert_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        stats.insert_end_time - insert_start).count();
    
    std::cout << "\n✓ Data insertion completed\n";
    std::cout << "  Stream S: " << stats.events_inserted_s << " events\n";
    std::cout << "  Stream R: " << stats.events_inserted_r << " events\n";
    std::cout << "  Total:    " << (stats.events_inserted_s + stats.events_inserted_r) << " events\n";
    std::cout << "  Duration: " << insert_duration_ms << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << ((insert_duration_ms > 0) ? all_events.size() * 1000.0 / insert_duration_ms : 0.0)
              << " events/s\n\n";
    
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
              << std::fixed << std::setprecision(2)
              << (stats.windows_triggered > 0 ? static_cast<double>(stats.join_results) / stats.windows_triggered : 0.0) << "\n\n";
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
    std::cout << "  - High disorder & large scale testing\n";
    std::cout << "  - Real PECJ benchmark datasets (CSV)\n";
    std::cout << "  - Out-of-order event simulation\n";
    std::cout << "  - Late arrival handling with watermark\n";
    std::cout << "  - All data stored in sageTSDB tables\n";
    std::cout << "  - PECJ as stateless compute engine\n";
    std::cout << "  - Multi-threaded sliding window processing\n";
#else
    std::cout << "  ⚠ Stub Mode (PECJ not integrated)\n";
    std::cout << "  - Only data loading and insertion tested\n";
    std::cout << "  - Rebuild with -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED to enable\n";
#endif
    std::cout << "\n";
    
    std::cout << "[Next Steps]\n";
    std::cout << "  1. Check build/sage_tsdb_data/lsm/ for persisted data\n";
    std::cout << "  2. Try higher disorder ratios: --disorder-ratio 0.5\n";
    std::cout << "  3. Test with more data: --max-s 500000 --max-r 500000\n";
    std::cout << "  4. Adjust window parameters: --window-us, --slide-us\n";
    std::cout << "  5. Stress test with late arrivals: --max-disorder-us 10000\n";
    std::cout << "\n";
    
    return 0;
}
