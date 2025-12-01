/**
 * @file performance_benchmark.cpp
 * @brief PECJ + sageTSDB 性能基准测试
 * 
 * 本程序提供全面的性能评估，用于：
 * 1. 测量不同 PECJ 算子的性能
 * 2. 评估不同数据规模下的吞吐量和延迟
 * 3. 比较不同配置的资源消耗
 * 4. 生成可视化的性能报告
 */

#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <map>
#include <fstream>
#include <cmath>

#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"
#include "sage_tsdb/utils/csv_data_loader.h"

using namespace sage_tsdb;
using namespace sage_tsdb::plugins;
using namespace sage_tsdb::utils;

// ============================================================================
// 基准测试配置
// ============================================================================

struct BenchmarkConfig {
    std::string s_file;
    std::string r_file;
    
    std::vector<std::string> operators = {"IMA", "SHJ", "MSWJ"};
    std::vector<size_t> tuple_counts = {1000, 5000, 10000, 50000};
    std::vector<int> thread_counts = {1, 2, 4, 8};
    
    uint64_t window_len_ms = 1000;
    uint64_t slide_len_ms = 500;
    
    int repeat_count = 3;  // 每个配置重复次数
    std::string output_csv = "benchmark_results.csv";
};

// ============================================================================
// 基准测试结果
// ============================================================================

struct BenchmarkResult {
    std::string operator_name;
    size_t tuple_count;
    int thread_count;
    
    double avg_throughput_ktps = 0.0;
    double avg_latency_ms = 0.0;
    double std_dev_throughput = 0.0;
    
    size_t join_results = 0;
    size_t windows_triggered = 0;
    
    std::vector<double> run_times_ms;
    
    void calculateStats() {
        if (run_times_ms.empty()) return;
        
        // 计算平均吞吐量
        double total_throughput = 0.0;
        for (double time_ms : run_times_ms) {
            if (time_ms > 0) {
                total_throughput += (tuple_count / time_ms);  // K tuples/sec
            }
        }
        avg_throughput_ktps = total_throughput / run_times_ms.size();
        
        // 计算平均延迟
        double total_time = 0.0;
        for (double time : run_times_ms) {
            total_time += time;
        }
        avg_latency_ms = total_time / run_times_ms.size();
        
        // 计算标准差
        if (run_times_ms.size() > 1) {
            double variance = 0.0;
            for (double time_ms : run_times_ms) {
                double throughput = (time_ms > 0) ? (tuple_count / time_ms) : 0.0;
                variance += std::pow(throughput - avg_throughput_ktps, 2);
            }
            std_dev_throughput = std::sqrt(variance / run_times_ms.size());
        }
    }
};

// ============================================================================
// 基准测试执行器
// ============================================================================

class BenchmarkRunner {
public:
    BenchmarkRunner(const BenchmarkConfig& config) : config_(config) {}
    
    std::vector<BenchmarkResult> runAll() {
        std::vector<BenchmarkResult> results;
        
        std::cout << "\n" << std::string(80, '=') << "\n";
        std::cout << "Starting Performance Benchmark\n";
        std::cout << std::string(80, '=') << "\n\n";
        
        size_t total_tests = config_.operators.size() * 
                            config_.tuple_counts.size() * 
                            config_.thread_counts.size();
        size_t current_test = 0;
        
        for (const auto& op : config_.operators) {
            for (size_t tuple_count : config_.tuple_counts) {
                for (int threads : config_.thread_counts) {
                    current_test++;
                    
                    std::cout << "\n[Test " << current_test << "/" << total_tests << "]\n";
                    std::cout << "  Operator: " << op << ", Tuples: " << tuple_count 
                              << ", Threads: " << threads << "\n";
                    
                    auto result = runSingleBenchmark(op, tuple_count, threads);
                    result.calculateStats();
                    results.push_back(result);
                    
                    std::cout << "  Avg Throughput: " << std::fixed << std::setprecision(2)
                              << result.avg_throughput_ktps << " K tuples/sec\n";
                    std::cout << "  Avg Latency: " << result.avg_latency_ms << " ms\n";
                }
            }
        }
        
        return results;
    }
    
private:
    BenchmarkResult runSingleBenchmark(const std::string& op, size_t tuple_count, int threads) {
        BenchmarkResult result;
        result.operator_name = op;
        result.tuple_count = tuple_count;
        result.thread_count = threads;
        
        // 加载数据
        CSVDataLoader s_loader(config_.s_file);
        CSVDataLoader r_loader(config_.r_file);
        
        auto s_tuples = s_loader.loadSortedByArrival(tuple_count / 2);
        auto r_tuples = r_loader.loadSortedByArrival(tuple_count / 2);
        
        // 合并
        struct TaggedTuple {
            PECJTuple tuple;
            bool is_s;
        };
        std::vector<TaggedTuple> all_tuples;
        for (const auto& t : s_tuples) all_tuples.push_back({t, true});
        for (const auto& t : r_tuples) all_tuples.push_back({t, false});
        std::sort(all_tuples.begin(), all_tuples.end(),
            [](const TaggedTuple& a, const TaggedTuple& b) {
                return a.tuple.arrivalTime < b.tuple.arrivalTime;
            });
        
        // 多次运行取平均
        for (int run = 0; run < config_.repeat_count; run++) {
            auto start = std::chrono::steady_clock::now();
            
            // 初始化插件
            PluginManager plugin_mgr;
            plugin_mgr.initialize();
            
            PluginConfig pecj_config = {
                {"windowLen", std::to_string(config_.window_len_ms * 1000)},
                {"slideLen", std::to_string(config_.slide_len_ms * 1000)},
                {"operator", op},
                {"threads", std::to_string(threads)},
                {"sLen", "10000"},
                {"rLen", "10000"}
            };
            
            plugin_mgr.loadPlugin("pecj", pecj_config);
            plugin_mgr.startAll();
            
            // 喂入数据
            for (const auto& tagged : all_tuples) {
                auto data = std::make_shared<TimeSeriesData>();
                data->timestamp = tagged.tuple.eventTime;
                data->value = tagged.tuple.value;
                data->tags["key"] = std::to_string(tagged.tuple.key);
                data->tags["stream"] = tagged.is_s ? "S" : "R";
                
                plugin_mgr.feedDataToAll(data);
            }
            
            // 等待完成
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            auto end = std::chrono::steady_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start).count();
            
            result.run_times_ms.push_back(duration_ms);
            
            // 收集统计（最后一次）
            if (run == config_.repeat_count - 1) {
                auto stats = plugin_mgr.getAllStats();
                if (stats.find("pecj") != stats.end()) {
                    auto& pecj_stats = stats["pecj"];
                    if (pecj_stats.find("windows_triggered") != pecj_stats.end()) {
                        result.windows_triggered = static_cast<size_t>(pecj_stats["windows_triggered"]);
                    }
                    if (pecj_stats.find("join_results") != pecj_stats.end()) {
                        result.join_results = static_cast<size_t>(pecj_stats["join_results"]);
                    }
                }
            }
            
            plugin_mgr.stopAll();
        }
        
        return result;
    }
    
    BenchmarkConfig config_;
};

// ============================================================================
// 结果输出
// ============================================================================

void printResultsTable(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n" << std::string(120, '=') << "\n";
    std::cout << "Benchmark Results Summary\n";
    std::cout << std::string(120, '=') << "\n";
    
    std::cout << std::left
              << std::setw(12) << "Operator"
              << std::setw(12) << "Tuples"
              << std::setw(10) << "Threads"
              << std::setw(20) << "Throughput(K/s)"
              << std::setw(15) << "Latency(ms)"
              << std::setw(15) << "Windows"
              << std::setw(15) << "Join Results"
              << "\n";
    std::cout << std::string(120, '-') << "\n";
    
    for (const auto& result : results) {
        std::cout << std::left
                  << std::setw(12) << result.operator_name
                  << std::setw(12) << result.tuple_count
                  << std::setw(10) << result.thread_count
                  << std::setw(20) << (std::to_string((int)result.avg_throughput_ktps) + " ± " + 
                                       std::to_string((int)result.std_dev_throughput))
                  << std::setw(15) << std::fixed << std::setprecision(2) << result.avg_latency_ms
                  << std::setw(15) << result.windows_triggered
                  << std::setw(15) << result.join_results
                  << "\n";
    }
    
    std::cout << std::string(120, '=') << "\n\n";
}

void saveResultsToCSV(const std::vector<BenchmarkResult>& results, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Failed to open output file: " << filename << "\n";
        return;
    }
    
    // CSV Header
    file << "Operator,TupleCount,ThreadCount,AvgThroughput_KTps,StdDevThroughput,"
         << "AvgLatency_ms,Windows,JoinResults\n";
    
    for (const auto& result : results) {
        file << result.operator_name << ","
             << result.tuple_count << ","
             << result.thread_count << ","
             << std::fixed << std::setprecision(2) << result.avg_throughput_ktps << ","
             << result.std_dev_throughput << ","
             << result.avg_latency_ms << ","
             << result.windows_triggered << ","
             << result.join_results << "\n";
    }
    
    file.close();
    std::cout << "[INFO] Results saved to: " << filename << "\n";
}

// ============================================================================
// 主函数
// ============================================================================

void printHeader() {
    std::cout << R"(
╔══════════════════════════════════════════════════════════════════════════╗
║                  sageTSDB Performance Benchmark Suite                    ║
║                      PECJ Algorithm Evaluation                           ║
╚══════════════════════════════════════════════════════════════════════════╝
)" << std::endl;
}

int main(int argc, char** argv) {
    printHeader();
    
    BenchmarkConfig config;
    
    // 解析参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--s-file" && i + 1 < argc) {
            config.s_file = argv[++i];
        } else if (arg == "--r-file" && i + 1 < argc) {
            config.r_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            config.output_csv = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --s-file <path>    S stream file\n"
                      << "  --r-file <path>    R stream file\n"
                      << "  --output <path>    Output CSV file\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }
    
    if (config.s_file.empty()) {
        config.s_file = "../../../PECJ/benchmark/datasets/sTuple.csv";
    }
    if (config.r_file.empty()) {
        config.r_file = "../../../PECJ/benchmark/datasets/rTuple.csv";
    }
    
    std::cout << "[Configuration]\n";
    std::cout << "  S File: " << config.s_file << "\n";
    std::cout << "  R File: " << config.r_file << "\n";
    std::cout << "  Operators: ";
    for (const auto& op : config.operators) std::cout << op << " ";
    std::cout << "\n  Tuple Counts: ";
    for (size_t count : config.tuple_counts) std::cout << count << " ";
    std::cout << "\n  Thread Counts: ";
    for (int tc : config.thread_counts) std::cout << tc << " ";
    std::cout << "\n  Repeat Count: " << config.repeat_count << "\n";
    std::cout << "  Output: " << config.output_csv << "\n";
    
    // 运行基准测试
    BenchmarkRunner runner(config);
    auto results = runner.runAll();
    
    // 输出结果
    printResultsTable(results);
    saveResultsToCSV(results, config.output_csv);
    
    std::cout << "\n[Benchmark Completed Successfully]\n\n";
    
    return 0;
}
