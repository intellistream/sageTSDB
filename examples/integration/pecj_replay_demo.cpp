/**
 * @file pecj_replay_demo.cpp
 * @brief PECJ 完整重放演示程序
 * 
 * 本程序展示如何使用真实的 PECJ 数据集进行流式 Join 操作，
 * 适合向客户展示 sageTSDB + PECJ 的集成效果。
 * 
 * 功能特性：
 * 1. 从 PECJ 真实数据集加载 S 流和 R 流数据
 * 2. 按照到达时间顺序重放数据
 * 3. 实时展示窗口触发和 Join 结果
 * 4. 统计关键性能指标（吞吐量、延迟、匹配率）
 * 5. 支持多种 PECJ 算子（IMA、MSWJ、SHJ 等）
 * 
 * 编译运行：
 *   cd build
 *   make pecj_replay_demo
 *   ./examples/pecj_replay_demo --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
 *                                --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
 *                                --max-tuples 10000
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <algorithm>
#include <map>
#include <cmath>

#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"
#include "sage_tsdb/utils/csv_data_loader.h"

using namespace sage_tsdb;
using namespace sage_tsdb::plugins;
using namespace sage_tsdb::utils;

// ============================================================================
// 配置结构
// ============================================================================

struct DemoConfig {
    std::string s_file;           // S 流数据文件
    std::string r_file;           // R 流数据文件
    size_t max_tuples = 0;        // 最大处理元组数（0=全部）
    
    // PECJ 配置
    std::string operator_type = "IMA";  // 算子类型
    uint64_t window_len_ms = 1000;      // 窗口长度（毫秒）
    uint64_t slide_len_ms = 500;        // 滑动步长（毫秒）
    uint64_t lateness_ms = 100;         // 允许延迟（毫秒）
    int threads = 4;                     // PECJ 线程数
    
    // 展示配置
    bool verbose = true;           // 详细输出
    bool realtime_replay = false;  // 按真实时间戳重放（模拟实时流）
    size_t display_interval = 1000; // 每处理多少个元组显示一次进度
};

// ============================================================================
// 性能统计
// ============================================================================

struct PerformanceStats {
    size_t s_tuples_processed = 0;
    size_t r_tuples_processed = 0;
    size_t total_tuples_processed = 0;
    size_t windows_triggered = 0;
    size_t join_results = 0;
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    double getThroughputKTps() const {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        if (duration == 0) return 0.0;
        return (total_tuples_processed / (double)duration);  // K tuples/sec
    }
    
    double getProcessingTimeMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
    }
    
    double getJoinSelectivity() const {
        if (total_tuples_processed == 0) return 0.0;
        return (join_results / (double)total_tuples_processed) * 100.0;
    }
    
    void print() const {
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Performance Statistics\n";
        std::cout << std::string(80, '=') << "\n";
        std::cout << std::left << std::setw(35) << "S Stream Tuples:" 
                  << s_tuples_processed << "\n";
        std::cout << std::left << std::setw(35) << "R Stream Tuples:" 
                  << r_tuples_processed << "\n";
        std::cout << std::left << std::setw(35) << "Total Tuples Processed:" 
                  << total_tuples_processed << "\n";
        std::cout << std::left << std::setw(35) << "Windows Triggered:" 
                  << windows_triggered << "\n";
        std::cout << std::left << std::setw(35) << "Join Results Generated:" 
                  << join_results << "\n";
        std::cout << std::left << std::setw(35) << "Processing Time (ms):" 
                  << std::fixed << std::setprecision(2) << getProcessingTimeMs() << "\n";
        std::cout << std::left << std::setw(35) << "Throughput (K tuples/sec):" 
                  << std::fixed << std::setprecision(2) << getThroughputKTps() << "\n";
        std::cout << std::left << std::setw(35) << "Join Selectivity (%):" 
                  << std::fixed << std::setprecision(2) << getJoinSelectivity() << "\n";
        std::cout << std::string(80, '=') << "\n\n";
    }
};

// ============================================================================
// 主函数
// ============================================================================

void printHeader() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════════════╗
║                   sageTSDB + PECJ Integration Demo                       ║
║                   Real-Time Stream Join with PECJ                        ║
╚══════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printConfig(const DemoConfig& config) {
    std::cout << "\n[Configuration]\n";
    std::cout << "  S Stream File     : " << config.s_file << "\n";
    std::cout << "  R Stream File     : " << config.r_file << "\n";
    std::cout << "  Max Tuples        : " << (config.max_tuples > 0 ? std::to_string(config.max_tuples) : "Unlimited") << "\n";
    std::cout << "  PECJ Operator     : " << config.operator_type << "\n";
    std::cout << "  Window Length     : " << config.window_len_ms << " ms\n";
    std::cout << "  Slide Length      : " << config.slide_len_ms << " ms\n";
    std::cout << "  Lateness Tolerance: " << config.lateness_ms << " ms\n";
    std::cout << "  PECJ Threads      : " << config.threads << "\n";
    std::cout << "  Realtime Replay   : " << (config.realtime_replay ? "Yes" : "No") << "\n";
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    printHeader();
    
    // ========================================================================
    // 1. 解析命令行参数
    // ========================================================================
    DemoConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--max-tuples" && i + 1 < argc) {
            config.max_tuples = std::stoull(argv[++i]);
        } else if (arg == "--operator" && i + 1 < argc) {
            config.operator_type = argv[++i];
        } else if (arg == "--window-ms" && i + 1 < argc) {
            config.window_len_ms = std::stoull(argv[++i]);
        } else if (arg == "--realtime") {
            config.realtime_replay = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --s-file <path>       Path to S stream CSV file\n"
                      << "  --r-file <path>       Path to R stream CSV file\n"
                      << "  --max-tuples <n>      Maximum tuples to process (default: all)\n"
                      << "  --operator <name>     PECJ operator (IMA, MSWJ, SHJ, default: IMA)\n"
                      << "  --window-ms <ms>      Window length in milliseconds (default: 1000)\n"
                      << "  --realtime            Replay with real timestamps (slower)\n"
                      << "  --help                Show this help\n";
            return 0;
        }
    }
    
    // 默认路径（相对于 build 目录）
    if (config.s_file.empty()) {
        config.s_file = "../../../PECJ/benchmark/datasets/sTuple.csv";
    }
    if (config.r_file.empty()) {
        config.r_file = "../../../PECJ/benchmark/datasets/rTuple.csv";
    }
    
    printConfig(config);
    
    // ========================================================================
    // 2. 加载数据集
    // ========================================================================
    std::cout << "[Loading Data]\n";
    
    CSVDataLoader s_loader(config.s_file);
    CSVDataLoader r_loader(config.r_file);
    
    std::vector<PECJTuple> s_tuples, r_tuples;
    
    try {
        std::cout << "  Loading S stream from: " << config.s_file << " ... " << std::flush;
        s_tuples = s_loader.loadSortedByArrival(config.max_tuples > 0 ? config.max_tuples / 2 : 0);
        std::cout << "OK (" << s_tuples.size() << " tuples)\n";
        
        std::cout << "  Loading R stream from: " << config.r_file << " ... " << std::flush;
        r_tuples = r_loader.loadSortedByArrival(config.max_tuples > 0 ? config.max_tuples / 2 : 0);
        std::cout << "OK (" << r_tuples.size() << " tuples)\n";
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Failed to load data: " << e.what() << std::endl;
        return 1;
    }
    
    if (s_tuples.empty() || r_tuples.empty()) {
        std::cerr << "[ERROR] No data loaded. Check file paths.\n";
        return 1;
    }
    
    std::cout << "  Total tuples to process: " << (s_tuples.size() + r_tuples.size()) << "\n\n";
    
    // ========================================================================
    // 3. 初始化 PECJ 插件
    // ========================================================================
    std::cout << "[Initializing PECJ Plugin]\n";
    
    PluginManager plugin_mgr;
    plugin_mgr.initialize();
    
    // 配置 PECJ
    PluginConfig pecj_config = {
        {"windowLen", std::to_string(config.window_len_ms * 1000)},  // 转换为微秒
        {"slideLen", std::to_string(config.slide_len_ms * 1000)},
        {"latenessMs", std::to_string(config.lateness_ms)},
        {"operator", config.operator_type},
        {"threads", std::to_string(config.threads)},
        {"sLen", "10000"},
        {"rLen", "10000"}
    };
    
    try {
        plugin_mgr.loadPlugin("pecj", pecj_config);
        plugin_mgr.startAll();
        std::cout << "  PECJ plugin initialized with " << config.operator_type << " operator\n\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Failed to initialize PECJ: " << e.what() << std::endl;
        return 1;
    }
    
    // ========================================================================
    // 4. 合并并按到达时间排序
    // ========================================================================
    struct TaggedTuple {
        PECJTuple tuple;
        bool is_s_stream;  // true = S stream, false = R stream
    };
    
    std::vector<TaggedTuple> all_tuples;
    all_tuples.reserve(s_tuples.size() + r_tuples.size());
    
    for (const auto& t : s_tuples) {
        all_tuples.push_back({t, true});
    }
    for (const auto& t : r_tuples) {
        all_tuples.push_back({t, false});
    }
    
    std::sort(all_tuples.begin(), all_tuples.end(),
        [](const TaggedTuple& a, const TaggedTuple& b) {
            return a.tuple.arrivalTime < b.tuple.arrivalTime;
        });
    
    // ========================================================================
    // 5. 重放数据流
    // ========================================================================
    std::cout << "[Replaying Data Stream]\n";
    std::cout << "  Progress: [" << std::string(50, ' ') << "] 0%\r" << std::flush;
    
    PerformanceStats stats;
    stats.start_time = std::chrono::steady_clock::now();
    
    uint64_t last_arrival_time = 0;
    size_t display_counter = 0;
    
    for (size_t i = 0; i < all_tuples.size(); i++) {
        const auto& tagged = all_tuples[i];
        const auto& tuple = tagged.tuple;
        
        // 实时重放模式：按真实时间间隔等待
        if (config.realtime_replay && last_arrival_time > 0) {
            uint64_t time_diff_us = tuple.arrivalTime - last_arrival_time;
            if (time_diff_us > 0 && time_diff_us < 1000000) {  // 最多等待 1 秒
                std::this_thread::sleep_for(std::chrono::microseconds(time_diff_us));
            }
        }
        last_arrival_time = tuple.arrivalTime;
        
        // 转换为 TimeSeriesData
        auto data = std::make_shared<TimeSeriesData>();
        data->timestamp = tuple.eventTime;
        data->value = tuple.value;
        data->tags["key"] = std::to_string(tuple.key);
        data->tags["stream"] = tagged.is_s_stream ? "S" : "R";
        data->tags["arrivalTime"] = std::to_string(tuple.arrivalTime);
        
        // 喂给 PECJ
        plugin_mgr.feedDataToAll(data);
        
        // 更新统计
        stats.total_tuples_processed++;
        if (tagged.is_s_stream) {
            stats.s_tuples_processed++;
        } else {
            stats.r_tuples_processed++;
        }
        
        // 显示进度
        display_counter++;
        if (display_counter >= config.display_interval || i == all_tuples.size() - 1) {
            int progress = (int)((i + 1) * 100 / all_tuples.size());
            int bar_width = 50;
            int filled = bar_width * progress / 100;
            
            std::cout << "  Progress: [" 
                      << std::string(filled, '=') 
                      << std::string(bar_width - filled, ' ') 
                      << "] " << progress << "% "
                      << "(" << (i + 1) << "/" << all_tuples.size() << ")"
                      << "\r" << std::flush;
            display_counter = 0;
        }
    }
    
    std::cout << std::endl;
    
    // 等待 PECJ 处理完剩余数据
    std::cout << "\n[Finalizing]\n";
    std::cout << "  Waiting for PECJ to flush remaining windows...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    stats.end_time = std::chrono::steady_clock::now();
    
    // ========================================================================
    // 6. 获取统计信息
    // ========================================================================
    auto plugin_stats = plugin_mgr.getAllStats();
    
    if (plugin_stats.find("pecj") != plugin_stats.end()) {
        auto& pecj_stats = plugin_stats["pecj"];
        
        // 提取 PECJ 特定统计
        if (pecj_stats.find("windows_triggered") != pecj_stats.end()) {
            stats.windows_triggered = static_cast<size_t>(pecj_stats["windows_triggered"]);
        }
        if (pecj_stats.find("join_results") != pecj_stats.end()) {
            stats.join_results = static_cast<size_t>(pecj_stats["join_results"]);
        }
        
        std::cout << "\n[PECJ Internal Stats]\n";
        for (const auto& [key, value] : pecj_stats) {
            std::cout << "  " << std::left << std::setw(30) << key << ": " << value << "\n";
        }
    }
    
    // ========================================================================
    // 7. 打印最终统计
    // ========================================================================
    stats.print();
    
    // ========================================================================
    // 8. 清理
    // ========================================================================
    plugin_mgr.stopAll();
    
    std::cout << "[Demo Completed Successfully]\n";
    std::cout << "\nTip: Run with --help to see all available options.\n\n";
    
    return 0;
}
