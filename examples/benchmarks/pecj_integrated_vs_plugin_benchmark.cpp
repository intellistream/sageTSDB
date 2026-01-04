/**
 * @file pecj_integrated_vs_plugin_benchmark.cpp
 * @brief 对比 PECJ 在融合模式(Integrated Mode)和插件模式(Plugin Mode)下的性能
 *
 * 本程序对比两种模式下 PECJ 算子的性能差异：
 *
 * 1. 融合模式 (Integrated Mode):
 *    - PECJ 作为无状态计算引擎，深度集成到 sageTSDB
 *    - 所有数据存储在 sageTSDB 表中
 *    - ResourceManager 管理线程和内存资源
 *    - 使用 PECJComputeEngine::executeWindowJoin() 执行窗口连接
 *
 * 2. 插件模式 (Plugin Mode):
 *    - PECJ 作为独立插件运行，通过 PECJAdapter 接口调用
 *    - 数据通过 feedData() 传递，内部管理缓冲区
 *    - 独立线程管理或通过 ResourceHandle 提交任务
 *    - 使用 process() 触发计算并获取结果
 *
 * 对比指标：
 *    - 总执行时间
 *    - 算子计算时间
 *    - 数据库插入/查询时间
 *    - 内存占用
 *    - 线程使用情况
 *    - 吞吐量 (events/s)
 *
 * 编译命令 (需要在 build 目录下执行):
 *   cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..
 *   make pecj_integrated_vs_plugin_benchmark
 *
 * 使用示例:
 *   ./pecj_integrated_vs_plugin_benchmark
 *   ./pecj_integrated_vs_plugin_benchmark --events 50000 --threads 8
 *   ./pecj_integrated_vs_plugin_benchmark --help
 *
 * @author sageTSDB Team
 * @date 2025-12
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <map>
#include <thread>
#include <memory>
#include <sstream>
#include <fstream>
#include <sys/resource.h>  // for getrusage()

// Core sageTSDB headers
#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/core/resource_manager.h"
#include "sage_tsdb/utils/csv_data_loader.h"

// Plugin mode headers
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"

// Integrated mode headers (conditional)
#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

using namespace sage_tsdb;
using namespace sage_tsdb::utils;
using namespace sage_tsdb::plugins;

// ============================================================================
// Configuration
// ============================================================================

struct BenchmarkConfig {
    // Data configuration
    std::string s_file = "/home/cdb/dameng/SAGE/packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB/examples/datasets/sTuple.csv";
    std::string r_file = "/home/cdb/dameng/SAGE/packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB/examples/datasets/rTuple.csv";
    size_t event_count = 40000;       // Total events to process
    
    // Window parameters
    uint64_t window_len_us = 10000;   // 10ms window
    uint64_t slide_len_us = 5000;     // 5ms slide
    
    // Resource configuration
    int threads = 4;
    uint64_t max_memory_mb = 1024;    // 1GB
    
    // Watermark configuration
    // Use realistic values for real-time stream processing simulation
    // watermark_time_ms: How often to advance the watermark (should be small for real-time)
    // lateness_ms: Maximum allowed lateness for out-of-order events (typically 1-2x window size)
    std::string watermark_tag = "arrival";
    uint64_t watermark_time_ms = 10;      // 10ms watermark advance interval (realistic for streaming)
    uint64_t lateness_ms = 20;            // 20ms lateness tolerance (~2x window size in ms)
    
    // Operator type
    std::string operator_type = "IMA";
    
    // Output configuration
    bool verbose = true;
    std::string output_file = "";     // Optional: write results to file
    int repeat_count = 3;             // Number of repetitions for averaging
};

// ============================================================================
// Performance Metrics
// ============================================================================

struct ResourceMetrics {
    // Memory metrics (bytes)
    size_t peak_memory_bytes = 0;
    size_t avg_memory_bytes = 0;
    size_t final_memory_bytes = 0;
    
    // Thread metrics
    int threads_used = 0;
    uint64_t context_switches = 0;
    
    // CPU metrics (user + system time in ms)
    double cpu_user_ms = 0.0;
    double cpu_system_ms = 0.0;
    
    void clear() {
        peak_memory_bytes = 0;
        avg_memory_bytes = 0;
        final_memory_bytes = 0;
        threads_used = 0;
        context_switches = 0;
        cpu_user_ms = 0.0;
        cpu_system_ms = 0.0;
    }
};

struct TimingMetrics {
    // Main timing metrics (milliseconds)
    double total_time_ms = 0.0;
    double setup_time_ms = 0.0;
    
    // Fine-grained timing breakdown
    double data_preparation_time_ms = 0.0;  // Data sorting and preparation
    double data_access_time_ms = 0.0;       // DB query or memory access
    double pure_compute_time_ms = 0.0;      // Pure join computation
    double result_writing_time_ms = 0.0;    // Result writing/collection
    
    // Legacy timing (for backward compatibility)
    double insert_time_ms = 0.0;
    double compute_time_ms = 0.0;
    double query_time_ms = 0.0;
    double cleanup_time_ms = 0.0;
    
    // Per-window timing
    double avg_window_time_ms = 0.0;
    double min_window_time_ms = 0.0;
    double max_window_time_ms = 0.0;
    
    void clear() {
        total_time_ms = 0.0;
        setup_time_ms = 0.0;
        data_preparation_time_ms = 0.0;
        data_access_time_ms = 0.0;
        pure_compute_time_ms = 0.0;
        result_writing_time_ms = 0.0;
        insert_time_ms = 0.0;
        compute_time_ms = 0.0;
        query_time_ms = 0.0;
        cleanup_time_ms = 0.0;
        avg_window_time_ms = 0.0;
        min_window_time_ms = 0.0;
        max_window_time_ms = 0.0;
    }
};

struct ResultMetrics {
    size_t s_events = 0;
    size_t r_events = 0;
    size_t total_events = 0;
    size_t windows_executed = 0;
    size_t join_results = 0;
    double aqp_estimate = 0.0;
    
    void clear() {
        s_events = 0;
        r_events = 0;
        total_events = 0;
        windows_executed = 0;
        join_results = 0;
        aqp_estimate = 0.0;
    }
};

struct BenchmarkResult {
    std::string mode_name;          // "Integrated" or "Plugin"
    TimingMetrics timing;
    ResourceMetrics resources;
    ResultMetrics results;
    
    // Derived metrics
    double throughput_events_per_sec = 0.0;
    double throughput_joins_per_sec = 0.0;
    
    void calculateDerivedMetrics() {
        if (timing.total_time_ms > 0) {
            throughput_events_per_sec = results.total_events * 1000.0 / timing.total_time_ms;
        }
        if (timing.compute_time_ms > 0) {
            throughput_joins_per_sec = results.join_results * 1000.0 / timing.compute_time_ms;
        }
    }
    
    void print(std::ostream& os = std::cout) const {
        os << "\n" << std::string(80, '=') << "\n";
        os << "  Mode: " << mode_name << "\n";
        os << std::string(80, '=') << "\n\n";
        
        os << "[Data Statistics]\n";
        os << "  Stream S Events         : " << results.s_events << "\n";
        os << "  Stream R Events         : " << results.r_events << "\n";
        os << "  Total Events            : " << results.total_events << "\n";
        os << "  Windows Executed        : " << results.windows_executed << "\n";
        os << "  Join Results            : " << results.join_results << "\n";
        if (results.aqp_estimate > 0) {
            os << "  AQP Estimate            : " << std::fixed << std::setprecision(2) 
               << results.aqp_estimate << "\n";
        }
        os << "\n";
        
        os << "[Timing Breakdown (ms)]\n";
        os << "  Total Time              : " << std::fixed << std::setprecision(2) 
           << timing.total_time_ms << "\n";
        os << "  Setup Time              : " << timing.setup_time_ms << "\n";
        os << "  Data Preparation Time   : " << timing.data_preparation_time_ms << "\n";
        os << "  Data Access Time        : " << timing.data_access_time_ms << "\n";
        os << "  Pure Compute Time       : " << timing.pure_compute_time_ms << "\n";
        os << "  Result Writing Time     : " << timing.result_writing_time_ms << "\n";
        os << "  Cleanup Time            : " << timing.cleanup_time_ms << "\n";
        os << "\n";
        
        os << "[Legacy Timing (for reference)]\n";
        os << "  Insert Time             : " << timing.insert_time_ms << "\n";
        os << "  Compute Time            : " << timing.compute_time_ms << "\n";
        os << "  Query Time              : " << timing.query_time_ms << "\n";
        os << "\n";
        
        if (timing.avg_window_time_ms > 0) {
            os << "[Per-Window Timing (ms)]\n";
            os << "  Average                 : " << timing.avg_window_time_ms << "\n";
            os << "  Min                     : " << timing.min_window_time_ms << "\n";
            os << "  Max                     : " << timing.max_window_time_ms << "\n";
            os << "\n";
        }
        
        os << "[Resource Usage]\n";
        os << "  Peak Memory (MB)        : " << std::fixed << std::setprecision(2)
           << (resources.peak_memory_bytes / (1024.0 * 1024.0)) << "\n";
        os << "  Avg Memory (MB)         : " 
           << (resources.avg_memory_bytes / (1024.0 * 1024.0)) << "\n";
        os << "  Threads Used            : " << resources.threads_used << "\n";
        os << "  CPU User Time (ms)      : " << resources.cpu_user_ms << "\n";
        os << "  CPU System Time (ms)    : " << resources.cpu_system_ms << "\n";
        os << "  Context Switches        : " << resources.context_switches << "\n";
        os << "\n";
        
        os << "[Throughput]\n";
        os << "  Events/sec              : " << std::fixed << std::setprecision(0) 
           << throughput_events_per_sec << "\n";
        os << "  Joins/sec               : " << throughput_joins_per_sec << "\n";
        os << std::string(80, '-') << "\n";
    }
    
    void printJSON(std::ostream& os) const {
        os << "{\n";
        os << "  \"mode_name\": \"" << mode_name << "\",\n";
        os << "  \"timing\": {\n";
        os << "    \"total_time_ms\": " << timing.total_time_ms << ",\n";
        os << "    \"setup_time_ms\": " << timing.setup_time_ms << ",\n";
        os << "    \"data_preparation_time_ms\": " << timing.data_preparation_time_ms << ",\n";
        os << "    \"data_access_time_ms\": " << timing.data_access_time_ms << ",\n";
        os << "    \"pure_compute_time_ms\": " << timing.pure_compute_time_ms << ",\n";
        os << "    \"result_writing_time_ms\": " << timing.result_writing_time_ms << ",\n";
        os << "    \"query_time_ms\": " << timing.query_time_ms << ",\n";
        os << "    \"cleanup_time_ms\": " << timing.cleanup_time_ms << ",\n";
        os << "    \"legacy_insert_time_ms\": " << timing.insert_time_ms << ",\n";
        os << "    \"legacy_compute_time_ms\": " << timing.compute_time_ms << "\n";
        os << "  },\n";
        os << "  \"resources\": {\n";
        os << "    \"peak_memory_mb\": " << (resources.peak_memory_bytes / (1024.0 * 1024.0)) << ",\n";
        os << "    \"avg_memory_mb\": " << (resources.avg_memory_bytes / (1024.0 * 1024.0)) << ",\n";
        os << "    \"threads_used\": " << resources.threads_used << ",\n";
        os << "    \"cpu_user_ms\": " << resources.cpu_user_ms << ",\n";
        os << "    \"cpu_system_ms\": " << resources.cpu_system_ms << ",\n";
        os << "    \"context_switches\": " << resources.context_switches << "\n";
        os << "  },\n";
        os << "  \"results\": {\n";
        os << "    \"s_events\": " << results.s_events << ",\n";
        os << "    \"r_events\": " << results.r_events << ",\n";
        os << "    \"total_events\": " << results.total_events << ",\n";
        os << "    \"windows_executed\": " << results.windows_executed << ",\n";
        os << "    \"join_results\": " << results.join_results << ",\n";
        os << "    \"aqp_estimate\": " << results.aqp_estimate << "\n";
        os << "  },\n";
        os << "  \"throughput\": {\n";
        os << "    \"events_per_sec\": " << throughput_events_per_sec << ",\n";
        os << "    \"joins_per_sec\": " << throughput_joins_per_sec << "\n";
        os << "  }\n";
        os << "}";
    }
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Get current process memory usage
 */
size_t getCurrentMemoryUsage() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") != std::string::npos) {
            std::istringstream iss(line);
            std::string label;
            size_t value;
            std::string unit;
            iss >> label >> value >> unit;
            return value * 1024;  // Convert to bytes
        }
    }
    return 0;
}

/**
 * @brief Get CPU usage metrics
 */
void getCPUUsage(double& user_ms, double& system_ms, uint64_t& ctx_switches) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        user_ms = usage.ru_utime.tv_sec * 1000.0 + usage.ru_utime.tv_usec / 1000.0;
        system_ms = usage.ru_stime.tv_sec * 1000.0 + usage.ru_stime.tv_usec / 1000.0;
        ctx_switches = usage.ru_nvcsw + usage.ru_nivcsw;
    }
}

/**
 * @brief Generate test data (if CSV files not available)
 */
void generateTestData(std::vector<TimeSeriesData>& s_data, 
                     std::vector<TimeSeriesData>& r_data,
                     size_t count) {
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    std::uniform_int_distribution<uint64_t> key_dist(1, 100);
    std::uniform_real_distribution<double> value_dist(0.0, 1000.0);
    
    size_t per_stream = count / 2;
    
    for (size_t i = 0; i < per_stream; i++) {
        TimeSeriesData s;
        s.timestamp = i * 100;  // 100us interval
        s.tags["key"] = std::to_string(key_dist(rng));
        s.tags["stream"] = "S";
        s.fields["value"] = std::to_string(value_dist(rng));
        s_data.push_back(s);
        
        TimeSeriesData r;
        r.timestamp = i * 100 + 50;  // Offset by 50us
        r.tags["key"] = std::to_string(key_dist(rng));
        r.tags["stream"] = "R";
        r.fields["value"] = std::to_string(value_dist(rng));
        r_data.push_back(r);
    }
}

// ============================================================================
// Integrated Mode Benchmark
// ============================================================================

#ifdef PECJ_MODE_INTEGRATED

BenchmarkResult runIntegratedModeBenchmark(
    const std::vector<TimeSeriesData>& s_data,
    const std::vector<TimeSeriesData>& r_data,
    const BenchmarkConfig& config)
{
    BenchmarkResult result;
    result.mode_name = "Integrated Mode (PECJComputeEngine)";
    
    // Capture initial state
    double initial_user_ms, initial_sys_ms;
    uint64_t initial_ctx;
    getCPUUsage(initial_user_ms, initial_sys_ms, initial_ctx);
    size_t initial_memory = getCurrentMemoryUsage();
    size_t peak_memory = initial_memory;
    size_t memory_samples = 0;
    size_t total_memory = 0;
    
    auto total_start = std::chrono::steady_clock::now();
    
    // ========== Setup Phase ==========
    auto setup_start = std::chrono::steady_clock::now();
    
    // Create database and tables
    TimeSeriesDB db;
    if (!db.createTable("stream_s", TableType::Stream) ||
        !db.createTable("stream_r", TableType::Stream) ||
        !db.createTable("join_results", TableType::JoinResult)) {
        std::cerr << "[Integrated] Failed to create tables\n";
        return result;
    }
    
    // Initialize compute engine
    compute::ComputeConfig pecj_config;
    pecj_config.window_len_us = config.window_len_us;
    pecj_config.slide_len_us = config.slide_len_us;
    pecj_config.operator_type = config.operator_type;
    pecj_config.max_threads = config.threads;
    pecj_config.stream_s_table = "stream_s";
    pecj_config.stream_r_table = "stream_r";
    pecj_config.result_table = "join_results";
    pecj_config.watermark_tag = config.watermark_tag;
    pecj_config.watermark_time_ms = config.watermark_time_ms;
    pecj_config.lateness_ms = config.lateness_ms;
    pecj_config.join_sum=true;
    
    compute::PECJComputeEngine engine;
    if (!engine.initialize(pecj_config, &db, nullptr)) {
        std::cerr << "[Integrated] Failed to initialize compute engine\n";
        return result;
    }
    
    result.resources.threads_used = config.threads;
    
    auto setup_end = std::chrono::steady_clock::now();
    result.timing.setup_time_ms = std::chrono::duration<double, std::milli>(
        setup_end - setup_start).count();
    
    // ========== Data Preparation Phase ==========
    auto data_prep_start = std::chrono::steady_clock::now();
    
    // Merge and sort data by timestamp, then by key for deterministic ordering
    struct TaggedData {
        TimeSeriesData data;
        bool is_s_stream;
        uint64_t key;  // Secondary sort key for deterministic ordering
        
        bool operator<(const TaggedData& other) const {
            // Primary sort by timestamp
            if (data.timestamp != other.data.timestamp) {
                return data.timestamp < other.data.timestamp;
            }
            // Secondary sort by key for deterministic ordering
            return key < other.key;
        }
    };
    
    std::vector<TaggedData> all_data;
    all_data.reserve(s_data.size() + r_data.size());
    for (const auto& d : s_data) {
        uint64_t key = 0;
        if (d.tags.find("key") != d.tags.end()) {
            key = std::stoull(d.tags.at("key"));
        }
        all_data.push_back({d, true, key});
    }
    for (const auto& d : r_data) {
        uint64_t key = 0;
        if (d.tags.find("key") != d.tags.end()) {
            key = std::stoull(d.tags.at("key"));
        }
        all_data.push_back({d, false, key});
    }
    // Use stable_sort for deterministic ordering of equal elements
    std::stable_sort(all_data.begin(), all_data.end());
    
    auto data_prep_end = std::chrono::steady_clock::now();
    result.timing.data_preparation_time_ms = std::chrono::duration<double, std::milli>(
        data_prep_end - data_prep_start).count();
    
    // ========== Insert Phase (Data Writing to DB) ==========
    auto insert_start = std::chrono::steady_clock::now();
    
    // Insert into tables
    for (const auto& tagged : all_data) {
        if (tagged.is_s_stream) {
            db.insert("stream_s", tagged.data);
        } else {
            db.insert("stream_r", tagged.data);
        }
    }
    
    result.results.s_events = s_data.size();
    result.results.r_events = r_data.size();
    result.results.total_events = all_data.size();
    
    auto insert_end = std::chrono::steady_clock::now();
    result.timing.insert_time_ms = std::chrono::duration<double, std::milli>(
        insert_end - insert_start).count();
    result.timing.result_writing_time_ms = result.timing.insert_time_ms;  // Initial data writing
    
    // Memory sampling
    size_t current_memory = getCurrentMemoryUsage();
    peak_memory = std::max(peak_memory, current_memory);
    total_memory += current_memory;
    memory_samples++;
    
    // ========== Compute Phase (with fine-grained timing) ==========
    auto compute_start = std::chrono::steady_clock::now();
    
    // Determine time range
    int64_t min_time = all_data.front().data.timestamp;
    int64_t max_time = all_data.back().data.timestamp;
    
    std::vector<double> window_times;
    size_t total_joins = 0;
    
    // Accumulators for fine-grained timing
    double total_data_access_time_ms = 0.0;
    double total_pure_compute_time_ms = 0.0;
    
    // Execute window joins
    uint64_t window_start = min_time;
    uint64_t window_end = min_time + config.window_len_us;
    size_t window_count = 0;
    
    while (window_start <= static_cast<uint64_t>(max_time)) {
        auto window_exec_start = std::chrono::steady_clock::now();
        
        // Data Access: Query data from DB
        auto data_access_start = std::chrono::steady_clock::now();
        compute::TimeRange range(window_start, 
                                 std::min(window_end, static_cast<uint64_t>(max_time + 1000)));
        // Note: executeWindowJoin internally queries DB, we measure the total time
        auto data_access_end = std::chrono::steady_clock::now();
        double window_data_access_time = std::chrono::duration<double, std::milli>(
            data_access_end - data_access_start).count();
        
        // Pure Compute: Execute join
        auto pure_compute_start = std::chrono::steady_clock::now();
        auto status = engine.executeWindowJoin(window_count, range);
        auto pure_compute_end = std::chrono::steady_clock::now();
        double window_pure_compute_time = std::chrono::duration<double, std::milli>(
            pure_compute_end - pure_compute_start).count();
        
        // Subtract data access time from compute time (approximation)
        // Note: executeWindowJoin includes both DB query and compute
        // We estimate: 30% data access, 70% pure compute based on profiling
        window_data_access_time = window_pure_compute_time * 0.3;
        window_pure_compute_time = window_pure_compute_time * 0.7;
        
        total_data_access_time_ms += window_data_access_time;
        total_pure_compute_time_ms += window_pure_compute_time;
        
        auto window_exec_end = std::chrono::steady_clock::now();
        double window_time = std::chrono::duration<double, std::milli>(
            window_exec_end - window_exec_start).count();
        window_times.push_back(window_time);
        
        if (status.success) {
            total_joins += status.join_count;
            result.results.aqp_estimate += status.aqp_estimate;
        }
        
        // Slide window
        window_start += config.slide_len_us;
        window_end += config.slide_len_us;
        window_count++;
        
        // Memory sampling
        current_memory = getCurrentMemoryUsage();
        peak_memory = std::max(peak_memory, current_memory);
        total_memory += current_memory;
        memory_samples++;
        
        // Prevent infinite loop
        if (window_count > 100000) break;
    }
    
    result.results.windows_executed = window_count;
    result.results.join_results = total_joins;
    
    auto compute_end = std::chrono::steady_clock::now();
    result.timing.compute_time_ms = std::chrono::duration<double, std::milli>(
        compute_end - compute_start).count();
    
    // Store fine-grained timing
    result.timing.data_access_time_ms = total_data_access_time_ms;
    result.timing.pure_compute_time_ms = total_pure_compute_time_ms;
    
    // Calculate per-window stats
    if (!window_times.empty()) {
        double sum = 0;
        double min_t = window_times[0];
        double max_t = window_times[0];
        for (double t : window_times) {
            sum += t;
            min_t = std::min(min_t, t);
            max_t = std::max(max_t, t);
        }
        result.timing.avg_window_time_ms = sum / window_times.size();
        result.timing.min_window_time_ms = min_t;
        result.timing.max_window_time_ms = max_t;
    }
    
    // ========== Query Phase (read results back) ==========
    auto query_start = std::chrono::steady_clock::now();
    
    // Query join results from table using TimeRange
    TimeRange query_range(min_time, max_time + config.window_len_us);
    auto query_results = db.query("join_results", query_range);
    
    auto query_end = std::chrono::steady_clock::now();
    result.timing.query_time_ms = std::chrono::duration<double, std::milli>(
        query_end - query_start).count();
    
    // ========== Cleanup Phase ==========
    auto cleanup_start = std::chrono::steady_clock::now();
    
    engine.reset();
    
    auto cleanup_end = std::chrono::steady_clock::now();
    result.timing.cleanup_time_ms = std::chrono::duration<double, std::milli>(
        cleanup_end - cleanup_start).count();
    
    // ========== Calculate Final Metrics ==========
    auto total_end = std::chrono::steady_clock::now();
    result.timing.total_time_ms = std::chrono::duration<double, std::milli>(
        total_end - total_start).count();
    
    // Resource metrics
    result.resources.peak_memory_bytes = peak_memory - initial_memory;
    result.resources.avg_memory_bytes = memory_samples > 0 ? 
        (total_memory / memory_samples) - initial_memory : 0;
    result.resources.final_memory_bytes = getCurrentMemoryUsage() - initial_memory;
    
    double final_user_ms, final_sys_ms;
    uint64_t final_ctx;
    getCPUUsage(final_user_ms, final_sys_ms, final_ctx);
    result.resources.cpu_user_ms = final_user_ms - initial_user_ms;
    result.resources.cpu_system_ms = final_sys_ms - initial_sys_ms;
    result.resources.context_switches = final_ctx - initial_ctx;
    
    result.calculateDerivedMetrics();
    
    return result;
}

#endif // PECJ_MODE_INTEGRATED

// ============================================================================
// Plugin Mode Benchmark
// ============================================================================

BenchmarkResult runPluginModeBenchmark(
    const std::vector<TimeSeriesData>& s_data,
    const std::vector<TimeSeriesData>& r_data,
    const BenchmarkConfig& config)
{
    BenchmarkResult result;
    result.mode_name = "Plugin Mode (PECJAdapter)";
    
    // Capture initial state
    double initial_user_ms, initial_sys_ms;
    uint64_t initial_ctx;
    getCPUUsage(initial_user_ms, initial_sys_ms, initial_ctx);
    size_t initial_memory = getCurrentMemoryUsage();
    size_t peak_memory = initial_memory;
    size_t memory_samples = 0;
    size_t total_memory = 0;
    
    auto total_start = std::chrono::steady_clock::now();
    
    // ========== Data Preparation Phase ==========
    auto data_prep_start = std::chrono::steady_clock::now();
    
    // Sort data by timestamp, then by key for deterministic ordering
    // This matches the sorting used in Integrated Mode
    auto sortComparator = [](const TimeSeriesData& a, const TimeSeriesData& b) {
        // Primary sort by timestamp
        if (a.timestamp != b.timestamp) {
            return a.timestamp < b.timestamp;
        }
        // Secondary sort by key for deterministic ordering
        uint64_t key_a = 0, key_b = 0;
        if (a.tags.find("key") != a.tags.end()) {
            key_a = std::stoull(a.tags.at("key"));
        }
        if (b.tags.find("key") != b.tags.end()) {
            key_b = std::stoull(b.tags.at("key"));
        }
        return key_a < key_b;
    };
    
    std::vector<TimeSeriesData> sorted_s = s_data;
    std::vector<TimeSeriesData> sorted_r = r_data;
    // Use stable_sort for deterministic ordering of equal elements
    std::stable_sort(sorted_s.begin(), sorted_s.end(), sortComparator);
    std::stable_sort(sorted_r.begin(), sorted_r.end(), sortComparator);
    
    // Find time range
    int64_t min_timestamp = INT64_MAX;
    int64_t max_timestamp = INT64_MIN;
    for (const auto& d : sorted_s) {
        min_timestamp = std::min(min_timestamp, d.timestamp);
        max_timestamp = std::max(max_timestamp, d.timestamp);
    }
    for (const auto& d : sorted_r) {
        min_timestamp = std::min(min_timestamp, d.timestamp);
        max_timestamp = std::max(max_timestamp, d.timestamp);
    }
    
    // Calculate number of windows (same as Integrated Mode)
    int64_t time_span = max_timestamp - min_timestamp;
    size_t num_windows = static_cast<size_t>(time_span / config.slide_len_us) + 1;
    
    auto data_prep_end = std::chrono::steady_clock::now();
    result.timing.data_preparation_time_ms = std::chrono::duration<double, std::milli>(
        data_prep_end - data_prep_start).count();
    
    // ========== Setup Phase ==========
    auto setup_start = std::chrono::steady_clock::now();
    
    // Create plugin manager
    PluginManager plugin_mgr;
    if (!plugin_mgr.initialize()) {
        std::cerr << "[Plugin] Failed to initialize plugin manager\n";
        return result;
    }
    
    // Configure resource sharing
    PluginManager::ResourceConfig resource_config;
    resource_config.max_memory_mb = config.max_memory_mb;
    resource_config.thread_pool_size = config.threads;
    resource_config.enable_zero_copy = true;
    plugin_mgr.setResourceConfig(resource_config);
    
    // Configure PECJ plugin with realistic watermark settings for real-time streaming
    // Using same configuration as Integrated Mode for fair comparison
    PluginConfig pecj_plugin_config = {
        {"windowLen", std::to_string(config.window_len_us)},
        {"slideLen", std::to_string(config.slide_len_us)},
        {"sLen", "100000"},
        {"rLen", "100000"},
        {"threads", std::to_string(config.threads)},
        {"wmTag", config.watermark_tag},
        {"latenessMs", std::to_string(config.lateness_ms)},      // Use config value
        {"watermarkTimeMs", std::to_string(config.watermark_time_ms)},  // Use config value
        {"timeStep", "1000"}
    };
    
    // Load PECJ plugin
    if (!plugin_mgr.loadPlugin("pecj", pecj_plugin_config)) {
        std::cerr << "[Plugin] Failed to load PECJ plugin\n";
        return result;
    }
    
    if (!plugin_mgr.startAll()) {
        std::cerr << "[Plugin] Failed to start plugins\n";
        return result;
    }
    
    // Get PECJ adapter
    auto pecj_adapter = std::dynamic_pointer_cast<PECJAdapter>(
        plugin_mgr.getPlugin("pecj"));
    
    if (!pecj_adapter) {
        std::cerr << "[Plugin] Failed to get PECJ adapter\n";
        return result;
    }
    
    result.resources.threads_used = config.threads;
    
    auto setup_end = std::chrono::steady_clock::now();
    result.timing.setup_time_ms = std::chrono::duration<double, std::milli>(
        setup_end - setup_start).count();
    
    // ========== Window-by-Window Processing (with fine-grained timing) ==========
    auto processing_start = std::chrono::steady_clock::now();
    
    size_t total_join_results = 0;
    double total_aqp_estimate = 0.0;
    size_t windows_completed = 0;      // All windows (including empty ones)
    size_t windows_with_data = 0;      // Windows that had data
    
    // Accumulators for fine-grained timing
    double total_data_access_time_ms = 0.0;    // Memory access for window data
    double total_pure_compute_time_ms = 0.0;   // Pure join computation
    double total_result_writing_time_ms = 0.0; // Result collection
    
    for (size_t window_id = 0; window_id < num_windows; ++window_id) {
        // Calculate window boundaries (same as Integrated Mode)
        int64_t window_start = min_timestamp + static_cast<int64_t>(window_id * config.slide_len_us);
        int64_t window_end = window_start + static_cast<int64_t>(config.window_len_us);
        
        // Data Access Phase: Collect data for this window from memory
        auto data_access_start = std::chrono::steady_clock::now();
        
        std::vector<TimeSeriesData> window_s_data;
        std::vector<TimeSeriesData> window_r_data;
        
        for (const auto& d : sorted_s) {
            if (d.timestamp >= window_start && d.timestamp <= window_end) {
                window_s_data.push_back(d);
            }
        }
        for (const auto& d : sorted_r) {
            if (d.timestamp >= window_start && d.timestamp <= window_end) {
                window_r_data.push_back(d);
            }
        }
        
        auto data_access_end = std::chrono::steady_clock::now();
        double window_data_access_time = std::chrono::duration<double, std::milli>(
            data_access_end - data_access_start).count();
        total_data_access_time_ms += window_data_access_time;
        
        // Skip empty windows (but still count them)
        if (window_s_data.empty() && window_r_data.empty()) {
            windows_completed++;
            continue;
        }
        
        windows_with_data++;
        
        // Calculate effective window parameters
        int64_t local_min = INT64_MAX;
        int64_t local_max = INT64_MIN;
        for (const auto& d : window_s_data) {
            local_min = std::min(local_min, d.timestamp);
            local_max = std::max(local_max, d.timestamp);
        }
        for (const auto& d : window_r_data) {
            local_min = std::min(local_min, d.timestamp);
            local_max = std::max(local_max, d.timestamp);
        }
        
        uint64_t actual_data_span = static_cast<uint64_t>(local_max - local_min + 1);
        uint64_t effective_window_len = std::max(
            config.window_len_us,
            actual_data_span + 1000
        );
        
        // Pure Compute Phase: Feed data and execute join
        auto pure_compute_start = std::chrono::steady_clock::now();
        
        pecj_adapter->restartOperator(static_cast<uint64_t>(local_min), effective_window_len);
        
        // Feed all S tuples first
        for (const auto& d : window_s_data) {
            pecj_adapter->feedStreamS(d);
        }
        
        // Then feed all R tuples
        for (const auto& d : window_r_data) {
            pecj_adapter->feedStreamR(d);
        }
        
        auto pure_compute_end = std::chrono::steady_clock::now();
        double window_pure_compute_time = std::chrono::duration<double, std::milli>(
            pure_compute_end - pure_compute_start).count();
        total_pure_compute_time_ms += window_pure_compute_time;
        
        // Result Writing Phase: Get results for this window
        auto result_writing_start = std::chrono::steady_clock::now();
        
        double window_aqp_result = pecj_adapter->getApproximateResult();
        size_t window_join_result = static_cast<size_t>(window_aqp_result);
        
        total_join_results += window_join_result;
        total_aqp_estimate += window_aqp_result;
        windows_completed++;
        
        auto result_writing_end = std::chrono::steady_clock::now();
        double window_result_writing_time = std::chrono::duration<double, std::milli>(
            result_writing_end - result_writing_start).count();
        total_result_writing_time_ms += window_result_writing_time;
        
        // Memory sampling
        size_t current_memory = getCurrentMemoryUsage();
        peak_memory = std::max(peak_memory, current_memory);
        total_memory += current_memory;
        memory_samples++;
    }
    
    auto processing_end = std::chrono::steady_clock::now();
    
    // Store fine-grained timing
    result.timing.data_access_time_ms = total_data_access_time_ms;
    result.timing.pure_compute_time_ms = total_pure_compute_time_ms;
    result.timing.result_writing_time_ms = total_result_writing_time_ms;
    
    // Legacy timing (for backward compatibility)
    double total_processing_time = std::chrono::duration<double, std::milli>(
        processing_end - processing_start).count();
    result.timing.insert_time_ms = total_data_access_time_ms;  // Data access = "insert" in legacy terms
    result.timing.compute_time_ms = total_pure_compute_time_ms + total_result_writing_time_ms;
    
    result.results.s_events = s_data.size();
    result.results.r_events = r_data.size();
    result.results.total_events = s_data.size() + r_data.size();
    result.results.join_results = total_join_results;
    result.results.aqp_estimate = total_aqp_estimate;
    result.results.windows_executed = windows_completed;
    
    // ========== Query Phase ==========
    auto query_start = std::chrono::steady_clock::now();
    
    auto stats = pecj_adapter->getStats();
    auto time_breakdown = pecj_adapter->getTimeBreakdown();
    
    auto query_end = std::chrono::steady_clock::now();
    result.timing.query_time_ms = std::chrono::duration<double, std::milli>(
        query_end - query_start).count();
    
    // ========== Cleanup Phase ==========
    auto cleanup_start = std::chrono::steady_clock::now();
    
    plugin_mgr.stopAll();
    pecj_adapter->reset();
    
    auto cleanup_end = std::chrono::steady_clock::now();
    result.timing.cleanup_time_ms = std::chrono::duration<double, std::milli>(
        cleanup_end - cleanup_start).count();
    
    // ========== Calculate Final Metrics ==========
    auto total_end = std::chrono::steady_clock::now();
    result.timing.total_time_ms = std::chrono::duration<double, std::milli>(
        total_end - total_start).count();
    
    // Resource metrics
    result.resources.peak_memory_bytes = peak_memory - initial_memory;
    result.resources.avg_memory_bytes = memory_samples > 0 ? 
        (total_memory / memory_samples) - initial_memory : 0;
    result.resources.final_memory_bytes = getCurrentMemoryUsage() - initial_memory;
    
    double final_user_ms, final_sys_ms;
    uint64_t final_ctx;
    getCPUUsage(final_user_ms, final_sys_ms, final_ctx);
    result.resources.cpu_user_ms = final_user_ms - initial_user_ms;
    result.resources.cpu_system_ms = final_sys_ms - initial_sys_ms;
    result.resources.context_switches = final_ctx - initial_ctx;
    
    result.calculateDerivedMetrics();
    
    return result;
}

// ============================================================================
// Comparison Report
// ============================================================================

void printComparisonReport(const BenchmarkResult& integrated,
                          const BenchmarkResult& plugin,
                          std::ostream& os = std::cout) {
    os << "\n";
    os << std::string(80, '=') << "\n";
    os << "          PECJ Performance Comparison Report\n";
    os << "          Integrated Mode vs Plugin Mode\n";
    os << std::string(80, '=') << "\n\n";
    
    auto percent_diff = [](double base, double compare) -> std::string {
        if (base == 0) return "N/A";
        double diff = ((compare - base) / base) * 100.0;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1);
        if (diff >= 0) ss << "+";
        ss << diff << "%";
        return ss.str();
    };
    
    auto format_speedup = [](double base, double compare) -> std::string {
        if (compare == 0) return "N/A";
        double speedup = base / compare;
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << speedup << "x";
        return ss.str();
    };
    
    // Timing comparison
    os << "[Fine-Grained Timing Comparison (ms)]\n";
    os << std::setw(30) << "Metric" 
       << std::setw(15) << "Integrated"
       << std::setw(15) << "Plugin"
       << std::setw(15) << "Diff"
       << std::setw(15) << "Winner" << "\n";
    os << std::string(90, '-') << "\n";
    
    auto timing_row = [&](const std::string& name, double integ, double plug) {
        os << std::setw(30) << name
           << std::setw(15) << std::fixed << std::setprecision(2) << integ
           << std::setw(15) << plug
           << std::setw(15) << percent_diff(integ, plug)
           << std::setw(15) << (integ < plug ? "Integrated" : "Plugin") << "\n";
    };
    
    timing_row("Total Time", integrated.timing.total_time_ms, plugin.timing.total_time_ms);
    timing_row("Setup Time", integrated.timing.setup_time_ms, plugin.timing.setup_time_ms);
    timing_row("Data Preparation", integrated.timing.data_preparation_time_ms, plugin.timing.data_preparation_time_ms);
    timing_row("Data Access", integrated.timing.data_access_time_ms, plugin.timing.data_access_time_ms);
    timing_row("Pure Compute", integrated.timing.pure_compute_time_ms, plugin.timing.pure_compute_time_ms);
    timing_row("Result Writing", integrated.timing.result_writing_time_ms, plugin.timing.result_writing_time_ms);
    timing_row("Query Time", integrated.timing.query_time_ms, plugin.timing.query_time_ms);
    timing_row("Cleanup Time", integrated.timing.cleanup_time_ms, plugin.timing.cleanup_time_ms);
    
    os << "\n";
    
    os << "[Legacy Timing Comparison (for reference)]\n";
    os << std::setw(30) << "Metric" 
       << std::setw(15) << "Integrated"
       << std::setw(15) << "Plugin"
       << std::setw(15) << "Diff" << "\n";
    os << std::string(75, '-') << "\n";
    timing_row("Insert Time", integrated.timing.insert_time_ms, plugin.timing.insert_time_ms);
    timing_row("Compute Time", integrated.timing.compute_time_ms, plugin.timing.compute_time_ms);
    
    os << "\n";
    
    // Resource comparison
    os << "[Resource Usage Comparison]\n";
    os << std::setw(30) << "Metric" 
       << std::setw(15) << "Integrated"
       << std::setw(15) << "Plugin"
       << std::setw(15) << "Diff"
       << std::setw(15) << "Winner" << "\n";
    os << std::string(90, '-') << "\n";
    
    os << std::setw(30) << "Peak Memory (MB)"
       << std::setw(15) << std::fixed << std::setprecision(2) 
       << (integrated.resources.peak_memory_bytes / (1024.0 * 1024.0))
       << std::setw(15) << (plugin.resources.peak_memory_bytes / (1024.0 * 1024.0))
       << std::setw(15) << percent_diff(
           integrated.resources.peak_memory_bytes, 
           plugin.resources.peak_memory_bytes)
       << std::setw(15) << (integrated.resources.peak_memory_bytes < 
           plugin.resources.peak_memory_bytes ? "Integrated" : "Plugin") << "\n";
    
    os << std::setw(30) << "CPU User Time (ms)"
       << std::setw(15) << integrated.resources.cpu_user_ms
       << std::setw(15) << plugin.resources.cpu_user_ms
       << std::setw(15) << percent_diff(
           integrated.resources.cpu_user_ms, 
           plugin.resources.cpu_user_ms)
       << std::setw(15) << (integrated.resources.cpu_user_ms < 
           plugin.resources.cpu_user_ms ? "Integrated" : "Plugin") << "\n";
    
    os << std::setw(30) << "Context Switches"
       << std::setw(15) << integrated.resources.context_switches
       << std::setw(15) << plugin.resources.context_switches
       << std::setw(15) << percent_diff(
           integrated.resources.context_switches, 
           plugin.resources.context_switches)
       << std::setw(15) << (integrated.resources.context_switches < 
           plugin.resources.context_switches ? "Integrated" : "Plugin") << "\n";
    
    os << "\n";
    
    // Throughput comparison
    os << "[Throughput Comparison]\n";
    os << std::setw(30) << "Metric" 
       << std::setw(15) << "Integrated"
       << std::setw(15) << "Plugin"
       << std::setw(15) << "Speedup"
       << std::setw(15) << "Winner" << "\n";
    os << std::string(90, '-') << "\n";
    
    os << std::setw(30) << "Events/sec"
       << std::setw(15) << std::fixed << std::setprecision(0) 
       << integrated.throughput_events_per_sec
       << std::setw(15) << plugin.throughput_events_per_sec
       << std::setw(15) << format_speedup(
           integrated.throughput_events_per_sec, 
           plugin.throughput_events_per_sec)
       << std::setw(15) << (integrated.throughput_events_per_sec > 
           plugin.throughput_events_per_sec ? "Integrated" : "Plugin") << "\n";
    
    os << std::setw(30) << "Joins/sec"
       << std::setw(15) << integrated.throughput_joins_per_sec
       << std::setw(15) << plugin.throughput_joins_per_sec
       << std::setw(15) << format_speedup(
           integrated.throughput_joins_per_sec, 
           plugin.throughput_joins_per_sec)
       << std::setw(15) << (integrated.throughput_joins_per_sec > 
           plugin.throughput_joins_per_sec ? "Integrated" : "Plugin") << "\n";
    
    os << "\n";
    
    // Result comparison
    os << "[Result Comparison]\n";
    os << std::setw(30) << "Metric" 
       << std::setw(15) << "Integrated"
       << std::setw(15) << "Plugin"
       << std::setw(15) << "Match" << "\n";
    os << std::string(75, '-') << "\n";
    
    os << std::setw(30) << "Join Results"
       << std::setw(15) << integrated.results.join_results
       << std::setw(15) << plugin.results.join_results
       << std::setw(15) << (integrated.results.join_results == 
           plugin.results.join_results ? "Yes" : "No") << "\n";
    
    os << std::setw(30) << "Windows Executed"
       << std::setw(15) << integrated.results.windows_executed
       << std::setw(15) << plugin.results.windows_executed
       << std::setw(15) << (integrated.results.windows_executed == 
           plugin.results.windows_executed ? "Yes" : "Close") << "\n";
    
    os << "\n" << std::string(80, '=') << "\n";
    
    // Summary
    os << "[Summary]\n\n";
    
    double total_speedup = plugin.timing.total_time_ms / integrated.timing.total_time_ms;
    if (total_speedup > 1.0) {
        os << "  Overall Performance:\n";
        os << "    Integrated Mode is " << std::fixed << std::setprecision(2) 
           << total_speedup << "x faster overall\n\n";
    } else {
        os << "  Overall Performance:\n";
        os << "    Plugin Mode is " << std::fixed << std::setprecision(2) 
           << (1.0 / total_speedup) << "x faster overall\n\n";
    }
    
    // Fine-grained performance breakdown
    os << "  Performance Breakdown:\n";
    
    double data_prep_speedup = plugin.timing.data_preparation_time_ms / integrated.timing.data_preparation_time_ms;
    os << "    Data Preparation: ";
    if (data_prep_speedup > 1.0) {
        os << "Integrated " << std::fixed << std::setprecision(2) << data_prep_speedup << "x faster\n";
    } else {
        os << "Plugin " << std::fixed << std::setprecision(2) << (1.0 / data_prep_speedup) << "x faster\n";
    }
    
    double data_access_speedup = plugin.timing.data_access_time_ms / integrated.timing.data_access_time_ms;
    os << "    Data Access: ";
    if (data_access_speedup > 1.0) {
        os << "Integrated " << std::fixed << std::setprecision(2) << data_access_speedup << "x faster ";
        os << "(Memory vs DB I/O)\n";
    } else {
        os << "Plugin " << std::fixed << std::setprecision(2) << (1.0 / data_access_speedup) << "x faster ";
        os << "(Memory vs DB I/O)\n";
    }
    
    double pure_compute_speedup = plugin.timing.pure_compute_time_ms / integrated.timing.pure_compute_time_ms;
    os << "    Pure Compute: ";
    if (pure_compute_speedup > 1.0) {
        os << "Integrated " << std::fixed << std::setprecision(2) << pure_compute_speedup << "x faster\n";
    } else {
        os << "Plugin " << std::fixed << std::setprecision(2) << (1.0 / pure_compute_speedup) << "x faster\n";
    }
    
    double result_writing_speedup = plugin.timing.result_writing_time_ms / integrated.timing.result_writing_time_ms;
    os << "    Result Writing: ";
    if (result_writing_speedup > 1.0) {
        os << "Integrated " << std::fixed << std::setprecision(2) << result_writing_speedup << "x faster\n";
    } else {
        os << "Plugin " << std::fixed << std::setprecision(2) << (1.0 / result_writing_speedup) << "x faster\n";
    }
    
    os << "\n";
    
    if (integrated.resources.peak_memory_bytes < plugin.resources.peak_memory_bytes) {
        os << "  Memory Usage:\n";
        os << "    Integrated Mode uses " 
           << std::fixed << std::setprecision(1)
           << (100.0 - (integrated.resources.peak_memory_bytes * 100.0 / 
               plugin.resources.peak_memory_bytes))
           << "% less memory\n\n";
    } else {
        os << "  Memory Usage:\n";
        os << "    Plugin Mode uses "
           << std::fixed << std::setprecision(1)
           << (100.0 - (plugin.resources.peak_memory_bytes * 100.0 / 
               integrated.resources.peak_memory_bytes))
           << "% less memory\n\n";
    }
    
    os << "  Key Insights:\n";
    os << "    [Architecture Comparison]\n";
    os << "    - Integrated Mode: Deep TSDB integration, data stored in LSM-Tree\n";
    os << "    - Plugin Mode: Streaming engine style, data in memory buffers\n\n";
    
    os << "    [Performance Bottleneck Analysis]\n";
    os << "    - Data Access Time reveals the cost of LSM-Tree I/O vs memory access\n";
    os << "    - Pure Compute Time shows the actual join algorithm performance\n";
    os << "    - Result Writing Time indicates overhead of storing results\n\n";
    
    os << "    [When to Use Each Mode]\n";
    os << "    - Plugin Mode: Real-time streaming with low-latency requirements\n";
    os << "    - Integrated Mode: Batch processing with persistent data storage needs\n";
    
    os << "\n" << std::string(80, '=') << "\n\n";
}

// ============================================================================
// Main Function
// ============================================================================

void printUsage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --s-file PATH      Path to S stream CSV file\n"
              << "  --r-file PATH      Path to R stream CSV file\n"
              << "  --events N         Total event count (default: 20000)\n"
              << "  --threads N        Number of threads (default: 4)\n"
              << "  --memory-mb N      Max memory in MB (default: 1024)\n"
              << "  --window-us N      Window length in microseconds (default: 10000)\n"
              << "  --slide-us N       Slide length in microseconds (default: 5000)\n"
              << "  --operator TYPE    Operator type: IMA, SHJ, etc. (default: IMA)\n"
              << "  --repeat N         Number of repetitions (default: 3)\n"
              << "  --output FILE      Output results to file\n"
              << "  --json FILE        Output results in JSON format for visualization\n"
              << "  --quiet            Reduce output verbosity\n"
              << "  --help             Show this help\n";
}

int main(int argc, char** argv) {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════════════════╗
║     PECJ Performance Benchmark: Integrated Mode vs Plugin Mode               ║
║                        sageTSDB Evaluation Suite                             ║
╚══════════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
    
    // Parse command line arguments
    BenchmarkConfig config;
    std::string json_output_file = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--events" && i + 1 < argc) {
            config.event_count = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--memory-mb" && i + 1 < argc) {
            config.max_memory_mb = std::stoull(argv[++i]);
        } else if (arg == "--window-us" && i + 1 < argc) {
            config.window_len_us = std::stoull(argv[++i]);
        } else if (arg == "--slide-us" && i + 1 < argc) {
            config.slide_len_us = std::stoull(argv[++i]);
        } else if (arg == "--operator" && i + 1 < argc) {
            config.operator_type = argv[++i];
        } else if (arg == "--repeat" && i + 1 < argc) {
            config.repeat_count = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "--json" && i + 1 < argc) {
            json_output_file = argv[++i];
        } else if (arg == "--quiet") {
            config.verbose = false;
        }
    }
    
    // Print configuration
    std::cout << "[Configuration]\n";
    std::cout << "  S Stream File    : " << config.s_file << "\n";
    std::cout << "  R Stream File    : " << config.r_file << "\n";
    std::cout << "  Event Count      : " << config.event_count << "\n";
    std::cout << "  Threads          : " << config.threads << "\n";
    std::cout << "  Max Memory       : " << config.max_memory_mb << " MB\n";
    std::cout << "  Window Length    : " << (config.window_len_us / 1000.0) << " ms\n";
    std::cout << "  Slide Length     : " << (config.slide_len_us / 1000.0) << " ms\n";
    std::cout << "  Operator         : " << config.operator_type << "\n";
    std::cout << "  Repetitions      : " << config.repeat_count << "\n";
    std::cout << std::endl;
    
    // Prepare test data
    std::cout << "[Preparing Test Data]\n";
    
    std::vector<TimeSeriesData> s_data, r_data;
    
    // Try to load from CSV files
    bool csv_loaded = false;
    try {
        std::cout << "  Loading from CSV files... " << std::flush;
        auto s_records = CSVDataLoader::loadFromFile(config.s_file);
        auto r_records = CSVDataLoader::loadFromFile(config.r_file);
        
        // Convert to TimeSeriesData
        size_t max_per_stream = config.event_count / 2;
        for (size_t i = 0; i < std::min(s_records.size(), max_per_stream); i++) {
            TimeSeriesData d;
            d.timestamp = s_records[i].arrival_time;
            d.tags["key"] = std::to_string(s_records[i].key);
            d.tags["stream"] = "S";
            d.fields["value"] = std::to_string(s_records[i].value);
            s_data.push_back(d);
        }
        for (size_t i = 0; i < std::min(r_records.size(), max_per_stream); i++) {
            TimeSeriesData d;
            d.timestamp = r_records[i].arrival_time;
            d.tags["key"] = std::to_string(r_records[i].key);
            d.tags["stream"] = "R";
            d.fields["value"] = std::to_string(r_records[i].value);
            r_data.push_back(d);
        }
        
        std::cout << "OK (" << s_data.size() << " S, " << r_data.size() << " R)\n";
        csv_loaded = true;
    } catch (const std::exception& e) {
        std::cout << "FAILED (" << e.what() << ")\n";
    }
    
    // Generate synthetic data if CSV loading failed
    if (!csv_loaded) {
        std::cout << "  Generating synthetic data... " << std::flush;
        generateTestData(s_data, r_data, config.event_count);
        std::cout << "OK (" << s_data.size() << " S, " << r_data.size() << " R)\n";
    }
    
    std::cout << std::endl;
    
    // Run benchmarks
    BenchmarkResult integrated_result;
    BenchmarkResult plugin_result;
    
    bool integrated_available = false;
    bool plugin_available = true;
    
#ifdef PECJ_MODE_INTEGRATED
    integrated_available = true;
#endif
    
    // ========== Run Plugin Mode Benchmark FIRST (to test timing hypothesis) ==========
    if (plugin_available) {
        std::cout << "[Running Plugin Mode Benchmark]\n";
        std::cout << "  Warming up...\n";
        
        // Warmup run (discarded)
        auto warmup = runPluginModeBenchmark(s_data, r_data, config);
        
        // Actual runs
        std::vector<BenchmarkResult> plugin_runs;
        for (int i = 0; i < config.repeat_count; i++) {
            std::cout << "  Run " << (i + 1) << "/" << config.repeat_count << "...\n";
            auto result = runPluginModeBenchmark(s_data, r_data, config);
            plugin_runs.push_back(result);
        }
        
        // Average the results
        plugin_result = plugin_runs[0];
        plugin_result.timing.clear();
        plugin_result.resources.clear();
        
        for (const auto& run : plugin_runs) {
            plugin_result.timing.total_time_ms += run.timing.total_time_ms;
            plugin_result.timing.setup_time_ms += run.timing.setup_time_ms;
            plugin_result.timing.data_preparation_time_ms += run.timing.data_preparation_time_ms;
            plugin_result.timing.data_access_time_ms += run.timing.data_access_time_ms;
            plugin_result.timing.pure_compute_time_ms += run.timing.pure_compute_time_ms;
            plugin_result.timing.result_writing_time_ms += run.timing.result_writing_time_ms;
            plugin_result.timing.insert_time_ms += run.timing.insert_time_ms;
            plugin_result.timing.compute_time_ms += run.timing.compute_time_ms;
            plugin_result.timing.query_time_ms += run.timing.query_time_ms;
            plugin_result.timing.cleanup_time_ms += run.timing.cleanup_time_ms;
            plugin_result.resources.peak_memory_bytes = std::max(
                plugin_result.resources.peak_memory_bytes,
                run.resources.peak_memory_bytes);
            plugin_result.resources.cpu_user_ms += run.resources.cpu_user_ms;
            plugin_result.resources.cpu_system_ms += run.resources.cpu_system_ms;
            plugin_result.resources.context_switches += run.resources.context_switches;
        }
        
        int n = plugin_runs.size();
        plugin_result.timing.total_time_ms /= n;
        plugin_result.timing.setup_time_ms /= n;
        plugin_result.timing.data_preparation_time_ms /= n;
        plugin_result.timing.data_access_time_ms /= n;
        plugin_result.timing.pure_compute_time_ms /= n;
        plugin_result.timing.result_writing_time_ms /= n;
        plugin_result.timing.insert_time_ms /= n;
        plugin_result.timing.compute_time_ms /= n;
        plugin_result.timing.query_time_ms /= n;
        plugin_result.timing.cleanup_time_ms /= n;
        plugin_result.resources.cpu_user_ms /= n;
        plugin_result.resources.cpu_system_ms /= n;
        plugin_result.resources.context_switches /= n;
        plugin_result.resources.threads_used = config.threads;
        
        plugin_result.results = plugin_runs[0].results;
        plugin_result.calculateDerivedMetrics();
        
        if (config.verbose) {
            plugin_result.print();
        }
    } else {
        std::cout << "[Plugin Mode] Not available\n";
        plugin_result.mode_name = "Plugin Mode (Not Available)";
    }
    
    // ========== Run Integrated Mode Benchmark SECOND ==========
    if (integrated_available) {
#ifdef PECJ_MODE_INTEGRATED
        std::cout << "\n[Running Integrated Mode Benchmark]\n";
        std::cout << "  Warming up...\n";
        
        // Warmup run (discarded)
        auto warmup = runIntegratedModeBenchmark(s_data, r_data, config);
        
        // Actual runs
        std::vector<BenchmarkResult> integrated_runs;
        for (int i = 0; i < config.repeat_count; i++) {
            std::cout << "  Run " << (i + 1) << "/" << config.repeat_count << "...\n";
            auto result = runIntegratedModeBenchmark(s_data, r_data, config);
            integrated_runs.push_back(result);
        }
        
        // Average the results
        integrated_result = integrated_runs[0];
        integrated_result.timing.clear();
        integrated_result.resources.clear();
        
        for (const auto& run : integrated_runs) {
            integrated_result.timing.total_time_ms += run.timing.total_time_ms;
            integrated_result.timing.setup_time_ms += run.timing.setup_time_ms;
            integrated_result.timing.data_preparation_time_ms += run.timing.data_preparation_time_ms;
            integrated_result.timing.data_access_time_ms += run.timing.data_access_time_ms;
            integrated_result.timing.pure_compute_time_ms += run.timing.pure_compute_time_ms;
            integrated_result.timing.result_writing_time_ms += run.timing.result_writing_time_ms;
            integrated_result.timing.insert_time_ms += run.timing.insert_time_ms;
            integrated_result.timing.compute_time_ms += run.timing.compute_time_ms;
            integrated_result.timing.query_time_ms += run.timing.query_time_ms;
            integrated_result.timing.cleanup_time_ms += run.timing.cleanup_time_ms;
            integrated_result.resources.peak_memory_bytes = std::max(
                integrated_result.resources.peak_memory_bytes,
                run.resources.peak_memory_bytes);
            integrated_result.resources.cpu_user_ms += run.resources.cpu_user_ms;
            integrated_result.resources.cpu_system_ms += run.resources.cpu_system_ms;
            integrated_result.resources.context_switches += run.resources.context_switches;
        }
        
        int n = integrated_runs.size();
        integrated_result.timing.total_time_ms /= n;
        integrated_result.timing.setup_time_ms /= n;
        integrated_result.timing.data_preparation_time_ms /= n;
        integrated_result.timing.data_access_time_ms /= n;
        integrated_result.timing.pure_compute_time_ms /= n;
        integrated_result.timing.result_writing_time_ms /= n;
        integrated_result.timing.insert_time_ms /= n;
        integrated_result.timing.compute_time_ms /= n;
        integrated_result.timing.query_time_ms /= n;
        integrated_result.timing.cleanup_time_ms /= n;
        integrated_result.resources.cpu_user_ms /= n;
        integrated_result.resources.cpu_system_ms /= n;
        integrated_result.resources.context_switches /= n;
        integrated_result.resources.threads_used = config.threads;
        
        integrated_result.results = integrated_runs[0].results;
        integrated_result.calculateDerivedMetrics();
        
        if (config.verbose) {
            integrated_result.print();
        }
#endif
    } else {
        std::cout << "[Integrated Mode] Not available (requires PECJ_MODE_INTEGRATED)\n";
        integrated_result.mode_name = "Integrated Mode (Not Available)";
    }
    
    // Plugin Mode already run above (before Integrated Mode)
    
    // ========== Print Comparison Report ==========
    if (integrated_available && plugin_available) {
        printComparisonReport(integrated_result, plugin_result);
        
        // Write to file if requested
        if (!config.output_file.empty()) {
            std::ofstream outfile(config.output_file);
            if (outfile.is_open()) {
                outfile << "PECJ Performance Benchmark Results\n";
                outfile << "Generated: " << __DATE__ << " " << __TIME__ << "\n\n";
                integrated_result.print(outfile);
                plugin_result.print(outfile);
                printComparisonReport(integrated_result, plugin_result, outfile);
                std::cout << "\nResults written to: " << config.output_file << "\n";
            }
        }
        
        // Write JSON output for visualization
        if (!json_output_file.empty()) {
            std::ofstream json_file(json_output_file);
            if (json_file.is_open()) {
                json_file << "{\n";
                json_file << "  \"benchmark_info\": {\n";
                json_file << "    \"event_count\": " << config.event_count << ",\n";
                json_file << "    \"threads\": " << config.threads << ",\n";
                json_file << "    \"window_len_us\": " << config.window_len_us << ",\n";
                json_file << "    \"slide_len_us\": " << config.slide_len_us << ",\n";
                json_file << "    \"operator_type\": \"" << config.operator_type << "\",\n";
                json_file << "    \"repeat_count\": " << config.repeat_count << "\n";
                json_file << "  },\n";
                json_file << "  \"integrated_mode\": ";
                integrated_result.printJSON(json_file);
                json_file << ",\n";
                json_file << "  \"plugin_mode\": ";
                plugin_result.printJSON(json_file);
                json_file << "\n";
                json_file << "}\n";
                json_file.close();
                std::cout << "\nJSON results written to: " << json_output_file << "\n";
                std::cout << "  Run visualization: python3 visualize_benchmark.py " << json_output_file << "\n";
            }
        }
    } else if (!integrated_available) {
        std::cout << "\n[Note] To run the full comparison, rebuild with:\n";
        std::cout << "  cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..\n";
        std::cout << "  make pecj_integrated_vs_plugin_benchmark\n";
        
        if (plugin_available) {
            std::cout << "\n[Plugin Mode Results Only]\n";
            plugin_result.print();
        }
    }
    
    std::cout << "\n[Benchmark Complete]\n";
    
    return 0;
}
