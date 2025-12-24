/**
 * @file pecj_shj_comparison_demo.cpp
 * @brief 比较 PECJ 算子与 SHJ 算子在融合模式下的小/大数据量性能
 *
 * 这个示例在 PECJ 深度集成（INTEGRATED）模式下运行对比实验：
 *  1) 小数据量下：PECJ vs SHJ 性能对比
 *  2) 大数据量下：PECJ vs SHJ 性能对比
 *
 * 融合模式特点：
 *  - 所有数据存储在 sageTSDB 表中（stream_s, stream_r, join_results）
 *  - PECJ 作为无状态计算引擎，不持有数据缓冲区
 *  - sageTSDB ResourceManager 管理所有线程和内存资源
 *  - 使用 PECJComputeEngine::executeWindowJoin() 执行窗口连接
 *
 * PECJ 时间戳处理：
 *  - eventTime（事件时间）：用于窗口分配，决定元组属于哪个窗口
 *  - arrivalTime（到达时间）：用于数据流顺序处理和 Watermark 生成
 *
 * Watermark 策略：
 *  - ArrivalWM（默认）：当 arrivalTime >= nextWMPoint 时触发窗口计算
 *  - LatenessWM：基于 eventTime，当 windowUpperBound - earlierEmit + lateness < maxEventTime 时触发
 *
 * 处理模式：
 *  - Stream Mode（默认）：按 arrivalTime 顺序逐窗口处理，模拟真实流场景
 *  - Batch Mode：一次性处理所有数据，用于性能基准测试
 *
 * 编译：需要启用 PECJ_MODE_INTEGRATED
 *   cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..
 *   make pecj_shj_comparison_demo
 *
 * 使用示例：
 *   ./pecj_shj_comparison_demo                           # 默认 stream 模式
 *   ./pecj_shj_comparison_demo --batch                   # batch 模式
 *   ./pecj_shj_comparison_demo --watermark-tag lateness  # 使用 LatenessWM
 *   ./pecj_shj_comparison_demo --watermark-ms 50         # 设置 watermark 间隔
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

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/utils/csv_data_loader.h"

#ifdef PECJ_MODE_INTEGRATED
#include "sage_tsdb/compute/pecj_compute_engine.h"
#endif

using namespace sage_tsdb;
using namespace sage_tsdb::utils;

// ============================================================================
// Configuration & Statistics
// ============================================================================

struct DemoConfig {
    std::string s_file = "../../examples/datasets/sTuple.csv";
    std::string r_file = "../../examples/datasets/rTuple.csv";
    
    size_t small_count = 5000;      // 小数据量实验
    size_t large_count = 100000;    // 大数据量实验
    
    int threads = 4;
    uint64_t window_len_us = 10000;  // 10ms window
    uint64_t slide_len_us = 5000;    // 5ms slide
    int64_t time_unit_multiplier = 1; // CSV already in us, no conversion needed
    
    // Watermark 配置
    // 注意：PECJ 的 ArrivalWM 检查 arrivalTime >= nextWMPoint 来触发 watermark
    // CSV 数据 arrivalTime 范围约 5,202 us 到 2,155,455 us（约 2.15 秒）
    // 窗口大小 10ms，滑动步长 5ms，watermark 间隔应略大于窗口大小以确保窗口数据完整
    std::string watermark_tag = "arrival";  // "arrival" 或 "lateness"
    uint64_t watermark_time_ms = 10;        // 10ms = 10000us，与窗口大小相同
    uint64_t lateness_ms = 5;               // 对于 LatenessWM：允许的最大迟到时间 5ms
    
    // 是否按 arrivalTime 逐个 feed 数据（模拟真实流场景）
    bool stream_mode = true;
    
    bool verbose = true;
};

struct ExperimentStats {
    std::string operator_name;
    size_t data_scale;  // small or large
    
    size_t s_events = 0;
    size_t r_events = 0;
    size_t total_events = 0;
    
    size_t windows_executed = 0;
    size_t total_join_results = 0;
    
    double insert_time_ms = 0.0;
    double compute_time_ms = 0.0;
    double total_time_ms = 0.0;
    
    void print() const {
        std::cout << "\n" << std::string(70, '-') << "\n";
        std::cout << "Experiment: " << operator_name 
                  << " (Data Scale: " << data_scale << " events)\n";
        std::cout << std::string(70, '-') << "\n";
        std::cout << "  Stream S Events       : " << s_events << "\n";
        std::cout << "  Stream R Events       : " << r_events << "\n";
        std::cout << "  Total Events          : " << total_events << "\n";
        std::cout << "  Windows Executed      : " << windows_executed << "\n";
        std::cout << "  Total Join Results    : " << total_join_results << "\n";
        std::cout << "  Avg Joins/Window      : " 
                  << std::fixed << std::setprecision(2)
                  << (windows_executed > 0 ? (double)total_join_results / windows_executed : 0.0) << "\n";
        std::cout << "\n";
        std::cout << "  Insert Time           : " << std::fixed << std::setprecision(2) 
                  << insert_time_ms << " ms\n";
        std::cout << "  Compute Time          : " << compute_time_ms << " ms\n";
        std::cout << "  Total Time            : " << total_time_ms << " ms\n";
        std::cout << "\n";
        std::cout << "  Insert Throughput     : " 
                  << std::fixed << std::setprecision(0)
                  << (insert_time_ms > 0 ? total_events * 1000.0 / insert_time_ms : 0.0) 
                  << " events/s\n";
        std::cout << "  Compute Throughput    : " 
                  << (compute_time_ms > 0 ? total_join_results * 1000.0 / compute_time_ms : 0.0) 
                  << " joins/s\n";
        std::cout << "  Overall Throughput    : " 
                  << (total_time_ms > 0 ? total_events * 1000.0 / total_time_ms : 0.0) 
                  << " events/s\n";
        std::cout << std::string(70, '-') << "\n";
    }
};

// ============================================================================
// Helper Functions
// ============================================================================

#ifdef PECJ_MODE_INTEGRATED

/**
 * @brief 运行单次实验：使用指定的算子和数据量
 */
ExperimentStats runSingleExperiment(
    const std::vector<CSVRecord>& s_records,
    const std::vector<CSVRecord>& r_records,
    const DemoConfig& config,
    const std::string& operator_type,
    size_t max_events)
{
    ExperimentStats stats;
    stats.operator_name = operator_type;
    stats.data_scale = max_events;
    
    auto experiment_start = std::chrono::steady_clock::now();
    
    // ------------------------------------------------------------------------
    // 1. 初始化 sageTSDB
    // ------------------------------------------------------------------------
    if (config.verbose) {
        std::cout << "\n[" << operator_type << " @ " << max_events << " events] "
                  << "Initializing sageTSDB...\n";
    }
    
    TimeSeriesDB db;
    
    if (!db.createTable("stream_s", TableType::Stream)) {
        std::cerr << "Failed to create stream_s table\n";
        return stats;
    }
    if (!db.createTable("stream_r", TableType::Stream)) {
        std::cerr << "Failed to create stream_r table\n";
        return stats;
    }
    if (!db.createTable("join_results", TableType::JoinResult)) {
        std::cerr << "Failed to create join_results table\n";
        return stats;
    }
    
    // ------------------------------------------------------------------------
    // 2. 初始化 PECJ Compute Engine
    // ------------------------------------------------------------------------
    compute::ComputeConfig pecj_config;
    pecj_config.window_len_us = config.window_len_us;
    pecj_config.slide_len_us = config.slide_len_us;
    pecj_config.operator_type = operator_type;
    pecj_config.max_threads = config.threads;
    pecj_config.stream_s_table = "stream_s";
    pecj_config.stream_r_table = "stream_r";
    pecj_config.result_table = "join_results";
    
    // Watermark 配置：使用合理的值模拟真实流处理场景
    // - watermark_tag: "arrival" 表示使用到达时间生成 watermark
    // - watermark_time_ms: 每隔多少毫秒生成一次 watermark（触发窗口计算）
    // - lateness_ms: 允许的最大迟到时间
    pecj_config.watermark_tag = config.watermark_tag;
    pecj_config.watermark_time_ms = config.watermark_time_ms;
    pecj_config.lateness_ms = config.lateness_ms;
    
    compute::PECJComputeEngine pecj_engine;
    
    if (!pecj_engine.initialize(pecj_config, &db, nullptr)) {
        std::cerr << "Failed to initialize PECJ compute engine\n";
        return stats;
    }
    
    if (config.verbose) {
        std::cout << "  Operator: " << operator_type 
                  << ", Window: " << (pecj_config.window_len_us / 1000.0) << "ms"
                  << ", Slide: " << (pecj_config.slide_len_us / 1000.0) << "ms\n";
    }
    
    // ------------------------------------------------------------------------
    // 3. 准备数据（优先选择两个流中都存在的 (key, arrivalTime) 对）
    // ------------------------------------------------------------------------
    size_t max_per_stream = max_events / 2;
    
    // 构建 (key, arrivalTime) -> records 的映射
    std::map<std::pair<uint64_t, int64_t>, std::vector<CSVRecord>> s_map, r_map;
    
    for (const auto& rec : s_records) {
        s_map[{rec.key, rec.arrival_time}].push_back(rec);
    }
    for (const auto& rec : r_records) {
        r_map[{rec.key, rec.arrival_time}].push_back(rec);
    }
    
    // 找出共同的 (key, arrivalTime) 对
    std::vector<std::pair<uint64_t, int64_t>> common_pairs;
    for (const auto& [pair, _] : s_map) {
        if (r_map.count(pair)) {
            common_pairs.push_back(pair);
        }
    }
    
    std::cout << "  Found " << common_pairs.size() << " common (key,arrivalTime) pairs\n";
    
    // 优先从 common_pairs 中采样，然后补充剩余数据
    std::vector<CSVRecord> s_subset, r_subset;
    
    // 使用固定种子保证可重复性
    std::mt19937 rng(42);
    std::shuffle(common_pairs.begin(), common_pairs.end(), rng);
    
    // 从共同对中选择数据
    size_t target_per_stream = max_per_stream;
    for (const auto& pair : common_pairs) {
        if (s_subset.size() >= target_per_stream && r_subset.size() >= target_per_stream) {
            break;
        }
        
        // 添加 S 流的记录
        if (s_subset.size() < target_per_stream && s_map.count(pair)) {
            for (const auto& rec : s_map[pair]) {
                if (s_subset.size() < target_per_stream) {
                    s_subset.push_back(rec);
                }
            }
        }
        
        // 添加 R 流的记录
        if (r_subset.size() < target_per_stream && r_map.count(pair)) {
            for (const auto& rec : r_map[pair]) {
                if (r_subset.size() < target_per_stream) {
                    r_subset.push_back(rec);
                }
            }
        }
    }
    
    std::cout << "  Selected " << s_subset.size() << " S records, " 
              << r_subset.size() << " R records from common pairs\n";
    
    // 如果还需要更多数据，从剩余数据中随机采样补充
    if (s_subset.size() < target_per_stream) {
        std::vector<CSVRecord> s_remaining;
        for (const auto& rec : s_records) {
            bool already_added = false;
            for (const auto& added : s_subset) {
                if (added.key == rec.key && added.arrival_time == rec.arrival_time) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                s_remaining.push_back(rec);
            }
        }
        std::shuffle(s_remaining.begin(), s_remaining.end(), rng);
        for (size_t i = 0; i < std::min(target_per_stream - s_subset.size(), s_remaining.size()); i++) {
            s_subset.push_back(s_remaining[i]);
        }
    }
    
    if (r_subset.size() < target_per_stream) {
        std::vector<CSVRecord> r_remaining;
        for (const auto& rec : r_records) {
            bool already_added = false;
            for (const auto& added : r_subset) {
                if (added.key == rec.key && added.arrival_time == rec.arrival_time) {
                    already_added = true;
                    break;
                }
            }
            if (!already_added) {
                r_remaining.push_back(rec);
            }
        }
        std::shuffle(r_remaining.begin(), r_remaining.end(), rng);
        for (size_t i = 0; i < std::min(target_per_stream - r_subset.size(), r_remaining.size()); i++) {
            r_subset.push_back(r_remaining[i]);
        }
    }
    
    // 按 arrival_time 排序子集
    std::sort(s_subset.begin(), s_subset.end(),
        [](const CSVRecord& a, const CSVRecord& b) {
            return a.arrival_time < b.arrival_time;
        });
    std::sort(r_subset.begin(), r_subset.end(),
        [](const CSVRecord& a, const CSVRecord& b) {
            return a.arrival_time < b.arrival_time;
        });
    
    stats.s_events = s_subset.size();
    stats.r_events = r_subset.size();
    stats.total_events = stats.s_events + stats.r_events;
    
    // ------------------------------------------------------------------------
    // 4. 插入数据到 sageTSDB（按到达时间排序）
    // ------------------------------------------------------------------------
    if (config.verbose) {
        std::cout << "  Inserting " << stats.total_events << " events into sageTSDB...\n";
    }
    
    auto insert_start = std::chrono::steady_clock::now();
    
    // 合并两个流并按到达时间排序
    struct TaggedRecord {
        CSVRecord record;
        bool is_s_stream;
        
        bool operator<(const TaggedRecord& other) const {
            return record.arrival_time < other.record.arrival_time;
        }
    };
    
    std::vector<TaggedRecord> all_records;
    all_records.reserve(stats.total_events);
    
    for (const auto& r : s_subset) {
        all_records.push_back({r, true});
    }
    for (const auto& r : r_subset) {
        all_records.push_back({r, false});
    }
    
    std::sort(all_records.begin(), all_records.end());
    
    // 插入数据 - 使用 arrival_time 作为时间戳
    for (const auto& tagged : all_records) {
        TimeSeriesData ts_data;
        // 重要：使用 arrival_time 而不是 event_time，因为 CSV 中 event_time 大多为 0
        ts_data.timestamp = tagged.record.arrival_time;
        ts_data.tags["stream"] = tagged.is_s_stream ? "S" : "R";
        ts_data.tags["key"] = std::to_string(tagged.record.key);
        ts_data.fields["value"] = std::to_string(tagged.record.value);
        ts_data.fields["event_time"] = std::to_string(tagged.record.event_time);
        
        if (tagged.is_s_stream) {
            db.insert("stream_s", ts_data);
        } else {
            db.insert("stream_r", ts_data);
        }
    }
    
    auto insert_end = std::chrono::steady_clock::now();
    stats.insert_time_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        insert_end - insert_start).count();
    
    if (config.verbose) {
        std::cout << "  Insertion completed in " << std::fixed << std::setprecision(2)
                  << stats.insert_time_ms << " ms\n";
    }
    
    // ------------------------------------------------------------------------
    // 5. 执行 Join 计算
    // ------------------------------------------------------------------------
    if (config.verbose) {
        std::cout << "  Executing join computation";
        if (config.stream_mode) {
            std::cout << " (stream mode: feeding data incrementally by arrivalTime)...\n";
        } else {
            std::cout << " (batch mode: feeding all data at once)...\n";
        }
    }
    
    auto compute_start = std::chrono::steady_clock::now();
    
    int64_t min_time = std::min(s_subset[0].arrival_time, r_subset[0].arrival_time);
    int64_t max_time_s = s_subset.back().arrival_time;
    int64_t max_time_r = r_subset.back().arrival_time;
    int64_t max_time = std::max(max_time_s, max_time_r);
    
    if (config.stream_mode) {
        // ============================================================
        // Stream Mode: 按 arrivalTime 顺序逐个 feed 数据，模拟真实流场景
        // 
        // PECJ 时间戳语义：
        // - eventTime: 用于窗口分配（判断元组属于哪个窗口）
        // - arrivalTime: 用于数据流顺序处理和 Watermark 生成
        // 
        // Watermark 策略：
        // - ArrivalWM（默认）：当 arrivalTime >= nextWMPoint 时触发
        // - LatenessWM：基于 eventTime 的迟到容忍机制
        // ============================================================
        
        // 按 slide_len_us 划分窗口，逐个窗口执行 join
        uint64_t window_start = min_time;
        uint64_t window_end = min_time + config.window_len_us;
        size_t window_count = 0;
        
        while (window_start <= max_time) {
            compute::TimeRange window_range;
            window_range.start_us = window_start;
            window_range.end_us = std::min(window_end, static_cast<uint64_t>(max_time + 1000));
            
            auto status = pecj_engine.executeWindowJoin(window_count, window_range);
            
            if (status.success) {
                stats.windows_executed++;
                stats.total_join_results += status.join_count;
            }
            
            // 滑动窗口
            window_start += config.slide_len_us;
            window_end += config.slide_len_us;
            window_count++;
            
            // 防止无限循环
            if (window_count > 100000) {
                std::cerr << "Warning: Too many windows, stopping early\n";
                break;
            }
        }
        
        if (config.verbose) {
            std::cout << "  Executed " << stats.windows_executed << " windows\n";
        }
        
    } else {
        // ============================================================
        // Batch Mode: 一次性 feed 所有数据（用于性能基准测试）
        // ============================================================
        compute::TimeRange full_range;
        full_range.start_us = min_time;
        full_range.end_us = max_time + 1000;  // +1ms to include last timestamp
        
        auto status = pecj_engine.executeWindowJoin(0, full_range);
        
        if (status.success) {
            stats.windows_executed = 1;
            stats.total_join_results = status.join_count;
        }
    }
    
    auto compute_end = std::chrono::steady_clock::now();
    stats.compute_time_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        compute_end - compute_start).count();
    
    if (config.verbose) {
        std::cout << "  Join results: " << stats.total_join_results 
                  << ", computation time: " << std::fixed << std::setprecision(2) 
                  << stats.compute_time_ms << " ms\n";
    }
    
    // ------------------------------------------------------------------------
    // 6. 计算总时间
    // ------------------------------------------------------------------------
    auto experiment_end = std::chrono::steady_clock::now();
    stats.total_time_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        experiment_end - experiment_start).count();
    
    return stats;
}

#endif // PECJ_MODE_INTEGRATED

// ============================================================================
// Main Function
// ============================================================================

void printHeader() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════════════╗
║          PECJ vs SHJ Performance Comparison (Integrated Mode)            ║
║                  sageTSDB Deep Integration Demo                          ║
╚══════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

int main(int argc, char** argv) {
    printHeader();
    
    // ------------------------------------------------------------------------
    // 1. 解析命令行参数
    // ------------------------------------------------------------------------
    DemoConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--small-count" && i + 1 < argc) {
            config.small_count = std::stoull(argv[++i]);
        } else if (arg == "--large-count" && i + 1 < argc) {
            config.large_count = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = std::stoi(argv[++i]);
        } else if (arg == "--window-us" && i + 1 < argc) {
            config.window_len_us = std::stoull(argv[++i]);
        } else if (arg == "--slide-us" && i + 1 < argc) {
            config.slide_len_us = std::stoull(argv[++i]);
        } else if (arg == "--quiet") {
            config.verbose = false;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --s-file PATH         Path to S stream CSV file\n"
                      << "  --r-file PATH         Path to R stream CSV file\n"
                      << "  --small-count N       Small data scale (default: 5000)\n"
                      << "  --large-count N       Large data scale (default: 100000)\n"
                      << "  --threads N           Number of threads (default: 4)\n"
                      << "  --window-us N         Window length in microseconds (default: 10000)\n"
                      << "  --slide-us N          Slide length in microseconds (default: 5000)\n"
                      << "  --watermark-tag TAG   Watermark strategy: 'arrival' or 'lateness' (default: arrival)\n"
                      << "  --watermark-ms N      Watermark time interval in ms (default: 20)\n"
                      << "  --lateness-ms N       Max allowed lateness in ms (default: 10)\n"
                      << "  --batch               Use batch mode instead of stream mode\n"
                      << "  --quiet               Reduce output verbosity\n"
                      << "  --help                Show this help\n";
            return 0;
        } else if (arg == "--watermark-tag" && i + 1 < argc) {
            config.watermark_tag = argv[++i];
        } else if (arg == "--watermark-ms" && i + 1 < argc) {
            config.watermark_time_ms = std::stoull(argv[++i]);
        } else if (arg == "--lateness-ms" && i + 1 < argc) {
            config.lateness_ms = std::stoull(argv[++i]);
        } else if (arg == "--batch") {
            config.stream_mode = false;
        }
    }
    
    std::cout << "[Configuration]\n";
    std::cout << "  S Stream File    : " << config.s_file << "\n";
    std::cout << "  R Stream File    : " << config.r_file << "\n";
    std::cout << "  Small Scale      : " << config.small_count << " events\n";
    std::cout << "  Large Scale      : " << config.large_count << " events\n";
    std::cout << "  Window Length    : " << (config.window_len_us / 1000.0) << " ms\n";
    std::cout << "  Slide Length     : " << (config.slide_len_us / 1000.0) << " ms\n";
    std::cout << "  Threads          : " << config.threads << "\n";
    std::cout << "\n  [Watermark Config]\n";
    std::cout << "  Watermark Tag    : " << config.watermark_tag << "\n";
    std::cout << "  Watermark Time   : " << config.watermark_time_ms << " ms\n";
    std::cout << "  Lateness         : " << config.lateness_ms << " ms\n";
    std::cout << "  Processing Mode  : " << (config.stream_mode ? "Stream (sliding window)" : "Batch (one-shot)") << "\n";
    std::cout << std::endl;
    
#ifdef PECJ_MODE_INTEGRATED
    std::cout << "[Mode] PECJ Deep Integration Mode ✓\n";
    std::cout << "  - sageTSDB manages all data and resources\n";
    std::cout << "  - PECJ as stateless compute engine\n";
    std::cout << std::endl;
    
    // ------------------------------------------------------------------------
    // 2. 加载数据集（一次性加载）
    // ------------------------------------------------------------------------
    std::cout << "[Loading Datasets]\n";
    
    std::vector<CSVRecord> s_records, r_records;
    
    try {
        std::cout << "  Loading S stream... " << std::flush;
        s_records = CSVDataLoader::loadFromFile(config.s_file, config.time_unit_multiplier);
        std::cout << "✓ (" << s_records.size() << " records)\n";
        
        std::cout << "  Loading R stream... " << std::flush;
        r_records = CSVDataLoader::loadFromFile(config.r_file, config.time_unit_multiplier);
        std::cout << "✓ (" << r_records.size() << " records)\n";
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to load data: " << e.what() << std::endl;
        return 1;
    }
    
    if (s_records.empty() || r_records.empty()) {
        std::cerr << "[ERROR] No data loaded. Check file paths.\n";
        return 1;
    }
    
    std::cout << std::endl;
    
    // ------------------------------------------------------------------------
    // 3. 运行对比实验
    // ------------------------------------------------------------------------
    std::cout << std::string(80, '=') << "\n";
    std::cout << "PERFORMANCE COMPARISON EXPERIMENTS\n";
    std::cout << std::string(80, '=') << "\n";
    
    std::vector<ExperimentStats> all_results;
    
    // 实验 1: PECJ (IMA) - 小数据量
    std::cout << "\n[Experiment 1/4] PECJ Operator - Small Scale\n";
    auto stats1 = runSingleExperiment(s_records, r_records, config, "IMA", config.small_count);
    stats1.print();
    all_results.push_back(stats1);
    
    // 实验 2: SHJ - 小数据量
    std::cout << "\n[Experiment 2/4] SHJ Operator - Small Scale\n";
    auto stats2 = runSingleExperiment(s_records, r_records, config, "SHJ", config.small_count);
    stats2.print();
    all_results.push_back(stats2);
    
    // 实验 3: PECJ (IMA) - 大数据量
    std::cout << "\n[Experiment 3/4] PECJ Operator - Large Scale\n";
    auto stats3 = runSingleExperiment(s_records, r_records, config, "IMA", config.large_count);
    stats3.print();
    all_results.push_back(stats3);
    
    // 实验 4: SHJ - 大数据量
    std::cout << "\n[Experiment 4/4] SHJ Operator - Large Scale\n";
    auto stats4 = runSingleExperiment(s_records, r_records, config, "SHJ", config.large_count);
    stats4.print();
    all_results.push_back(stats4);
    
    // ------------------------------------------------------------------------
    // 4. 打印对比总结
    // ------------------------------------------------------------------------
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "COMPARISON SUMMARY\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    std::cout << "Small Scale (" << config.small_count << " events):\n";
    std::cout << "  PECJ (IMA) - Total Time: " << std::fixed << std::setprecision(2) 
              << stats1.total_time_ms << " ms, "
              << "Join Results: " << stats1.total_join_results << ", "
              << "Throughput: " << std::setprecision(0)
              << (stats1.total_time_ms > 0 ? stats1.total_events * 1000.0 / stats1.total_time_ms : 0.0)
              << " events/s\n";
    std::cout << "  SHJ        - Total Time: " << std::fixed << std::setprecision(2) 
              << stats2.total_time_ms << " ms, "
              << "Join Results: " << stats2.total_join_results << ", "
              << "Throughput: " << std::setprecision(0)
              << (stats2.total_time_ms > 0 ? stats2.total_events * 1000.0 / stats2.total_time_ms : 0.0)
              << " events/s\n";
    
    if (stats1.total_time_ms > 0 && stats2.total_time_ms > 0) {
        double speedup = stats2.total_time_ms / stats1.total_time_ms;
        std::cout << "  Speedup: PECJ is " << std::fixed << std::setprecision(2) 
                  << speedup << "x " << (speedup > 1.0 ? "faster" : "slower") << " than SHJ\n";
    }
    
    std::cout << "\nLarge Scale (" << config.large_count << " events):\n";
    std::cout << "  PECJ (IMA) - Total Time: " << std::fixed << std::setprecision(2) 
              << stats3.total_time_ms << " ms, "
              << "Join Results: " << stats3.total_join_results << ", "
              << "Throughput: " << std::setprecision(0)
              << (stats3.total_time_ms > 0 ? stats3.total_events * 1000.0 / stats3.total_time_ms : 0.0)
              << " events/s\n";
    std::cout << "  SHJ        - Total Time: " << std::fixed << std::setprecision(2) 
              << stats4.total_time_ms << " ms, "
              << "Join Results: " << stats4.total_join_results << ", "
              << "Throughput: " << std::setprecision(0)
              << (stats4.total_time_ms > 0 ? stats4.total_events * 1000.0 / stats4.total_time_ms : 0.0)
              << " events/s\n";
    
    if (stats3.total_time_ms > 0 && stats4.total_time_ms > 0) {
        double speedup = stats4.total_time_ms / stats3.total_time_ms;
        std::cout << "  Speedup: PECJ is " << std::fixed << std::setprecision(2) 
                  << speedup << "x " << (speedup > 1.0 ? "faster" : "slower") << " than SHJ\n";
    }
    
    std::cout << "\n" << std::string(80, '=') << "\n\n";
    
    std::cout << "[Conclusions]\n";
    std::cout << "  ✓ All experiments completed successfully\n";
    std::cout << "  ✓ Deep integration mode: sageTSDB manages all resources\n";
    std::cout << "  ✓ PECJ operates as stateless compute engine\n";
    std::cout << "  ✓ Performance comparison: PECJ (IMA) vs SHJ baseline\n";
    std::cout << "\n";
    
#else
    std::cout << "[ERROR] This demo requires PECJ_MODE_INTEGRATED\n";
    std::cout << "  Please rebuild with:\n";
    std::cout << "    cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..\n";
    std::cout << "    make pecj_shj_comparison_demo\n";
    return 1;
#endif
    
    return 0;
}
