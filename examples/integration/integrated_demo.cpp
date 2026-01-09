/**
 * @file integrated_demo.cpp
 * @brief PECJ + 故障检测集成演示
 * 
 * 本程序展示 sageTSDB 的完整数据处理管道：
 * 1. 使用 PECJ 进行实时流 Join
 * 2. 对 Join 结果进行故障检测（Z-Score / VAE）
 * 3. 实时告警异常事件
 * 4. 生成完整的性能报告
 * 
 * 这是一个可以直接展示给客户的端到端 demo。
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <fstream>

#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"
#include "sage_tsdb/plugins/adapters/fault_detection_adapter.h"
#include "sage_tsdb/utils/csv_data_loader.h"

using namespace sage_tsdb;
using namespace sage_tsdb::plugins;
using namespace sage_tsdb::utils;

// ============================================================================
// 配置和统计
// ============================================================================

struct IntegratedDemoConfig {
    // 数据源
    std::string s_file;
    std::string r_file;
    size_t max_tuples = 10000;  // 默认处理 1 万条
    
    // PECJ 配置
    std::string pecj_operator = "IMA";
    uint64_t window_len_ms = 1000;
    uint64_t slide_len_ms = 500;
    int pecj_threads = 4;
    
    // 故障检测配置
    std::string detection_method = "zscore";  // zscore 或 vae
    size_t detection_window = 50;
    double detection_threshold = 3.0;
    
    // 输出配置
    std::string output_file = "integrated_demo_results.txt";
    bool enable_alerts = true;
};

struct IntegratedStats {
    std::atomic<size_t> tuples_processed{0};
    std::atomic<size_t> windows_triggered{0};
    std::atomic<size_t> anomalies_detected{0};
    std::atomic<size_t> join_results{0};
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;
    
    std::vector<std::string> alert_log;
    std::mutex alert_mutex;
    
    void logAlert(const std::string& message) {
        std::lock_guard<std::mutex> lock(alert_mutex);
        alert_log.push_back(message);
        anomalies_detected++;
    }
    
    void print(std::ostream& os = std::cout) const {
        os << "\n" << std::string(80, '=') << "\n";
        os << "Integrated Demo - Performance Report\n";
        os << std::string(80, '=') << "\n\n";
        
        os << "[Data Processing]\n";
        os << "  Total Tuples Processed    : " << tuples_processed << "\n";
        os << "  Windows Triggered         : " << windows_triggered << "\n";
        os << "  Join Results Generated    : " << join_results << "\n\n";
        
        os << "[Fault Detection]\n";
        os << "  Anomalies Detected        : " << anomalies_detected << "\n";
        os << "  Detection Rate            : " 
           << std::fixed << std::setprecision(2)
           << (tuples_processed > 0 ? (anomalies_detected * 100.0 / tuples_processed) : 0.0) 
           << "%\n\n";
        
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        os << "[Performance]\n";
        os << "  Processing Time (ms)      : " << duration_ms << "\n";
        os << "  Throughput (K tuples/sec) : " 
           << std::fixed << std::setprecision(2)
           << (duration_ms > 0 ? (tuples_processed / (double)duration_ms) : 0.0) << "\n\n";
        
        if (!alert_log.empty()) {
            os << "[Alert Log] (Last 10 alerts)\n";
            size_t start_idx = alert_log.size() > 10 ? alert_log.size() - 10 : 0;
            for (size_t i = start_idx; i < alert_log.size(); i++) {
                os << "  [" << (i + 1) << "] " << alert_log[i] << "\n";
            }
        }
        
        os << "\n" << std::string(80, '=') << "\n";
    }
};

// ============================================================================
// 事件处理回调
// ============================================================================

class DemoEventHandler {
public:
    DemoEventHandler(IntegratedStats& stats, bool enable_alerts)
        : stats_(stats), enable_alerts_(enable_alerts) {}
    
    void onWindowTriggered(const std::string& plugin_name, uint64_t window_id) {
        stats_.windows_triggered++;
        if (enable_alerts_ && window_id % 10 == 0) {
            std::cout << "  [INFO] Window #" << window_id << " completed\n";
        }
    }
    
    void onAnomalyDetected(double score, uint64_t timestamp, double value) {
        std::stringstream ss;
        ss << "Anomaly at t=" << timestamp << ", value=" << std::fixed 
           << std::setprecision(2) << value << ", score=" << score;
        
        stats_.logAlert(ss.str());
        
        if (enable_alerts_) {
            std::cout << "  [ALERT] " << ss.str() << "\n";
        }
    }
    
    void onJoinResult(size_t result_count) {
        stats_.join_results += result_count;
    }
    
private:
    IntegratedStats& stats_;
    bool enable_alerts_;
};

// ============================================================================
// 主函数
// ============================================================================

void printHeader() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════════════╗
║             sageTSDB Integrated Demo: PECJ + Fault Detection             ║
║                   Real-Time Stream Join with Anomaly Detection           ║
╚══════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

void printConfig(const IntegratedDemoConfig& config) {
    std::cout << "\n[Configuration]\n";
    std::cout << "  S Stream File          : " << config.s_file << "\n";
    std::cout << "  R Stream File          : " << config.r_file << "\n";
    std::cout << "  Max Tuples             : " << config.max_tuples << "\n";
    std::cout << "  PECJ Operator          : " << config.pecj_operator << "\n";
    std::cout << "  Window Length          : " << config.window_len_ms << " ms\n";
    std::cout << "  Detection Method       : " << config.detection_method << "\n";
    std::cout << "  Detection Threshold    : " << config.detection_threshold << "\n";
    std::cout << "  Output File            : " << config.output_file << "\n";
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    printHeader();
    
    // ========================================================================
    // 1. 配置
    // ========================================================================
    IntegratedDemoConfig config;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--max-tuples" && i + 1 < argc) {
            config.max_tuples = std::stoull(argv[++i]);
        } else if (arg == "--detection" && i + 1 < argc) {
            config.detection_method = argv[++i];
        } else if (arg == "--threshold" && i + 1 < argc) {
            config.detection_threshold = std::stod(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_file = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --s-file <path>       S stream file\n"
                      << "  --r-file <path>       R stream file\n"
                      << "  --max-tuples <n>      Maximum tuples\n"
                      << "  --detection <method>  Detection method (zscore/vae)\n"
                      << "  --threshold <val>     Detection threshold\n"
                      << "  --output <path>       Output file path\n"
                      << "  --help                Show this help\n";
            return 0;
        }
    }
    
    if (config.s_file.empty()) {
        config.s_file = "../../../PECJ/benchmark/datasets/sTuple.csv";
    }
    if (config.r_file.empty()) {
        config.r_file = "../../../PECJ/benchmark/datasets/rTuple.csv";
    }
    
    printConfig(config);
    
    // ========================================================================
    // 2. 加载数据
    // ========================================================================
    std::cout << "[Loading Data]\n";
    
    CSVDataLoader s_loader(config.s_file);
    CSVDataLoader r_loader(config.r_file);
    
    std::vector<PECJTuple> s_tuples, r_tuples;
    
    try {
        std::cout << "  Loading S stream ... " << std::flush;
        s_tuples = s_loader.loadSortedByArrival(config.max_tuples / 2);
        std::cout << "OK (" << s_tuples.size() << " tuples)\n";
        
        std::cout << "  Loading R stream ... " << std::flush;
        r_tuples = r_loader.loadSortedByArrival(config.max_tuples / 2);
        std::cout << "OK (" << r_tuples.size() << " tuples)\n\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
    
    // ========================================================================
    // 3. 初始化插件管理器
    // ========================================================================
    std::cout << "[Initializing Plugins]\n";
    
    PluginManager plugin_mgr;
    plugin_mgr.initialize();
    
    // 配置资源
    PluginManager::ResourceConfig res_config;
    res_config.thread_pool_size = 8;
    res_config.max_memory_mb = 2048;
    res_config.enable_zero_copy = true;
    plugin_mgr.setResourceConfig(res_config);
    
    // 配置 PECJ
    PluginConfig pecj_config = {
        {"windowLen", std::to_string(config.window_len_ms * 1000)},
        {"slideLen", std::to_string(config.slide_len_ms * 1000)},
        {"operator", config.pecj_operator},
        {"threads", std::to_string(config.pecj_threads)},
        {"sLen", "10000"},
        {"rLen", "10000"}
    };
    
    // 配置故障检测
    PluginConfig fault_config = {
        {"method", config.detection_method},
        {"window_size", std::to_string(config.detection_window)},
        {"threshold", std::to_string(config.detection_threshold)}
    };
    
    try {
        plugin_mgr.loadPlugin("pecj", pecj_config);
        plugin_mgr.loadPlugin("fault_detection", fault_config);
        plugin_mgr.startAll();
        std::cout << "  PECJ plugin initialized (" << config.pecj_operator << ")\n";
        std::cout << "  Fault detection initialized (" << config.detection_method << ")\n\n";
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
    
    // ========================================================================
    // 4. 准备数据流
    // ========================================================================
    struct TaggedTuple {
        PECJTuple tuple;
        bool is_s_stream;
    };
    
    std::vector<TaggedTuple> all_tuples;
    all_tuples.reserve(s_tuples.size() + r_tuples.size());
    
    for (const auto& t : s_tuples) all_tuples.push_back({t, true});
    for (const auto& t : r_tuples) all_tuples.push_back({t, false});
    
    std::sort(all_tuples.begin(), all_tuples.end(),
        [](const TaggedTuple& a, const TaggedTuple& b) {
            return a.tuple.arrivalTime < b.tuple.arrivalTime;
        });
    
    // ========================================================================
    // 5. 执行数据流处理
    // ========================================================================
    std::cout << "[Processing Stream]\n";
    
    IntegratedStats stats;
    DemoEventHandler event_handler(stats, config.enable_alerts);
    stats.start_time = std::chrono::steady_clock::now();
    
    size_t progress_interval = all_tuples.size() / 20;  // 每 5% 显示一次
    if (progress_interval == 0) progress_interval = 1;
    
    for (size_t i = 0; i < all_tuples.size(); i++) {
        const auto& tagged = all_tuples[i];
        const auto& tuple = tagged.tuple;
        
        // 转换数据
        auto data = std::make_shared<TimeSeriesData>();
        data->timestamp = tuple.eventTime;
        data->value = tuple.value;
        data->tags["key"] = std::to_string(tuple.key);
        data->tags["stream"] = tagged.is_s_stream ? "S" : "R";
        
        // 喂入插件
        plugin_mgr.feedDataToAll(data);
        stats.tuples_processed++;
        
        // 显示进度
        if (i % progress_interval == 0) {
            int progress = (int)((i + 1) * 100 / all_tuples.size());
            std::cout << "  Progress: " << progress << "% (" 
                      << (i + 1) << "/" << all_tuples.size() << ")\n";
        }
    }
    
    std::cout << "\n[Finalizing]\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    stats.end_time = std::chrono::steady_clock::now();
    
    // ========================================================================
    // 6. 收集插件统计
    // ========================================================================
    auto plugin_stats = plugin_mgr.getAllStats();
    
    std::cout << "\n[Plugin Statistics]\n";
    for (const auto& [plugin_name, plugin_data] : plugin_stats) {
        std::cout << "  " << plugin_name << ":\n";
        for (const auto& [key, value] : plugin_data) {
            std::cout << "    " << std::left << std::setw(25) << key << ": " << value << "\n";
        }
    }
    
    // ========================================================================
    // 7. 生成报告
    // ========================================================================
    std::cout << "\n[Generating Report]\n";
    
    // 控制台输出
    stats.print(std::cout);
    
    // 文件输出
    std::ofstream out_file(config.output_file);
    if (out_file.is_open()) {
        out_file << "sageTSDB Integrated Demo Report\n";
        out_file << "Generated at: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n\n";
        stats.print(out_file);
        out_file.close();
        std::cout << "  Report saved to: " << config.output_file << "\n";
    } else {
        std::cerr << "  [WARNING] Failed to write report file\n";
    }
    
    // ========================================================================
    // 8. 清理
    // ========================================================================
    plugin_mgr.stopAll();
    
    std::cout << "\n[Demo Completed Successfully]\n\n";
    
    return 0;
}
