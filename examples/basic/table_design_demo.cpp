#include "sage_tsdb/core/table_manager.h"
#include "sage_tsdb/core/stream_table.h"
#include "sage_tsdb/core/join_result_table.h"
#include <iostream>
#include <chrono>
#include <random>

using namespace sage_tsdb;

/**
 * @brief 示例：完整的 PECJ 数据流演示
 * 
 * 演示场景：
 * 1. 创建 stream_s 和 stream_r 输入表
 * 2. 创建 join_results 输出表
 * 3. 模拟数据写入
 * 4. 模拟窗口 Join 计算
 * 5. 查询 Join 结果
 */

// 生成随机的股票交易数据
TimeSeriesData generateStockData(int64_t timestamp, const std::string& symbol, double price) {
    TimeSeriesData data;
    data.timestamp = timestamp;
    data.value = price;
    data.tags["symbol"] = symbol;
    data.tags["type"] = "trade";
    data.fields["volume"] = std::to_string(std::rand() % 1000);
    return data;
}

int main() {
    std::cout << "========== sageTSDB Table Design Demo ==========" << std::endl;
    
    // ========== 1. 初始化 Table Manager ==========
    std::cout << "\n[1] Initializing TableManager..." << std::endl;
    TableManager table_mgr("/tmp/sage_tsdb_data");
    
    // 为 PECJ 创建标准表集合
    bool success = table_mgr.createPECJTables("stock_");
    if (!success) {
        std::cerr << "Failed to create PECJ tables!" << std::endl;
        return 1;
    }
    
    std::cout << "Created tables:" << std::endl;
    for (const auto& name : table_mgr.listTables()) {
        std::cout << "  - " << name << std::endl;
    }
    
    // ========== 2. 获取表引用 ==========
    std::cout << "\n[2] Getting table references..." << std::endl;
    auto stream_s = table_mgr.getStreamTable("stock_stream_s");
    auto stream_r = table_mgr.getStreamTable("stock_stream_r");
    auto join_results = table_mgr.getJoinResultTable("stock_join_results");
    
    if (!stream_s || !stream_r || !join_results) {
        std::cerr << "Failed to get table references!" << std::endl;
        return 1;
    }
    
    std::cout << "Got table references successfully." << std::endl;
    
    // ========== 3. 模拟数据写入 ==========
    std::cout << "\n[3] Inserting stock data..." << std::endl;
    
    int64_t base_time = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "AMZN"};
    
    // 写入 1000 条数据到 stream_s
    for (int i = 0; i < 1000; i++) {
        int64_t timestamp = base_time + i * 100; // 每 100ms 一条数据
        std::string symbol = symbols[i % symbols.size()];
        double price = 100.0 + (std::rand() % 100) / 10.0;
        
        stream_s->insert(generateStockData(timestamp, symbol, price));
    }
    
    // 写入 800 条数据到 stream_r（模拟不同的事件率）
    for (int i = 0; i < 800; i++) {
        int64_t timestamp = base_time + i * 125; // 每 125ms 一条数据
        std::string symbol = symbols[i % symbols.size()];
        double price = 100.0 + (std::rand() % 100) / 10.0;
        
        stream_r->insert(generateStockData(timestamp, symbol, price));
    }
    
    std::cout << "Inserted " << stream_s->size() << " records to stream_s" << std::endl;
    std::cout << "Inserted " << stream_r->size() << " records to stream_r" << std::endl;
    
    // ========== 4. 查询窗口数据 ==========
    std::cout << "\n[4] Querying window data..." << std::endl;
    
    // 定义窗口：1秒窗口
    TimeRange window1(base_time, base_time + 1000);
    
    auto s_data = stream_s->query(window1);
    auto r_data = stream_r->query(window1);
    
    std::cout << "Window [" << window1.start_time << ", " << window1.end_time << "]:" << std::endl;
    std::cout << "  Stream S: " << s_data.size() << " records" << std::endl;
    std::cout << "  Stream R: " << r_data.size() << " records" << std::endl;
    
    // ========== 5. 模拟 PECJ 计算并写入结果 ==========
    std::cout << "\n[5] Simulating PECJ computation..." << std::endl;
    
    // 模拟 Join 计算（这里只是示例，实际会调用 PECJ）
    size_t join_count = 0;
    for (const auto& s_record : s_data) {
        for (const auto& r_record : r_data) {
            // 简单的 Join 条件：相同 symbol
            if (s_record.tags.at("symbol") == r_record.tags.at("symbol")) {
                join_count++;
            }
        }
    }
    
    // 创建 Join 结果记录
    JoinResultTable::JoinRecord result;
    result.window_id = 1;
    result.timestamp = window1.end_time;
    result.join_count = join_count;
    result.selectivity = static_cast<double>(join_count) / (s_data.size() * r_data.size());
    
    // 设置计算指标
    result.metrics.computation_time_ms = 5.2;
    result.metrics.memory_used_bytes = 1024 * 1024; // 1MB
    result.metrics.threads_used = 4;
    result.metrics.cpu_usage_percent = 85.0;
    result.metrics.used_aqp = false;
    result.metrics.algorithm_type = "IAWJ";
    
    result.tags["query"] = "stock_join_q1";
    
    // 写入结果
    join_results->insertJoinResult(result);
    
    std::cout << "Window 1 Join Results:" << std::endl;
    std::cout << "  Join Count: " << join_count << std::endl;
    std::cout << "  Selectivity: " << result.selectivity << std::endl;
    std::cout << "  Computation Time: " << result.metrics.computation_time_ms << " ms" << std::endl;
    
    // ========== 6. 多窗口计算 ==========
    std::cout << "\n[6] Computing multiple windows..." << std::endl;
    
    for (int win_id = 2; win_id <= 5; win_id++) {
        TimeRange window(base_time + (win_id - 1) * 1000, 
                        base_time + win_id * 1000);
        
        auto s_win = stream_s->query(window);
        auto r_win = stream_r->query(window);
        
        size_t win_join_count = 0;
        for (const auto& s_rec : s_win) {
            for (const auto& r_rec : r_win) {
                if (s_rec.tags.at("symbol") == r_rec.tags.at("symbol")) {
                    win_join_count++;
                }
            }
        }
        
        JoinResultTable::JoinRecord win_result;
        win_result.window_id = win_id;
        win_result.timestamp = window.end_time;
        win_result.join_count = win_join_count;
        win_result.selectivity = s_win.empty() || r_win.empty() ? 0.0 :
            static_cast<double>(win_join_count) / (s_win.size() * r_win.size());
        win_result.metrics.computation_time_ms = 4.0 + (std::rand() % 20) / 10.0;
        win_result.metrics.memory_used_bytes = 1024 * 1024;
        win_result.metrics.threads_used = 4;
        win_result.metrics.cpu_usage_percent = 80.0 + (std::rand() % 15);
        win_result.metrics.algorithm_type = "IAWJ";
        
        join_results->insertJoinResult(win_result);
        
        std::cout << "  Window " << win_id << ": " << win_join_count 
                  << " joins (" << s_win.size() << " x " << r_win.size() << ")" << std::endl;
    }
    
    // ========== 7. 查询 Join 结果 ==========
    std::cout << "\n[7] Querying join results..." << std::endl;
    
    // 按窗口 ID 查询
    auto window1_results = join_results->queryByWindow(1);
    std::cout << "Window 1 results: " << window1_results.size() << " records" << std::endl;
    if (!window1_results.empty()) {
        const auto& r = window1_results[0];
        std::cout << "  Join Count: " << r.join_count << std::endl;
        std::cout << "  Computation Time: " << r.metrics.computation_time_ms << " ms" << std::endl;
    }
    
    // 查询最近 3 个窗口
    auto latest = join_results->queryLatest(3);
    std::cout << "\nLatest 3 windows:" << std::endl;
    for (const auto& r : latest) {
        std::cout << "  Window " << r.window_id << ": " 
                  << r.join_count << " joins, "
                  << r.metrics.computation_time_ms << " ms" << std::endl;
    }
    
    // 聚合统计
    TimeRange full_range(base_time, base_time + 10000);
    auto agg_stats = join_results->queryAggregateStats(full_range);
    std::cout << "\nAggregate Statistics:" << std::endl;
    std::cout << "  Total Windows: " << agg_stats.total_windows << std::endl;
    std::cout << "  Total Joins: " << agg_stats.total_joins << std::endl;
    std::cout << "  Avg Join/Window: " << agg_stats.avg_join_count << std::endl;
    std::cout << "  Avg Computation Time: " << agg_stats.avg_computation_time_ms << " ms" << std::endl;
    std::cout << "  Avg Selectivity: " << agg_stats.avg_selectivity << std::endl;
    
    // ========== 8. 表统计信息 ==========
    std::cout << "\n[8] Table statistics..." << std::endl;
    
    auto s_stats = stream_s->getStats();
    std::cout << "Stream S Stats:" << std::endl;
    std::cout << "  Total Records: " << s_stats.total_records << std::endl;
    std::cout << "  MemTable Records: " << s_stats.memtable_records << std::endl;
    std::cout << "  Time Range: [" << s_stats.min_timestamp 
              << ", " << s_stats.max_timestamp << "]" << std::endl;
    std::cout << "  Indexes: " << s_stats.num_indexes << std::endl;
    
    auto join_stats = join_results->getStats();
    std::cout << "\nJoin Results Stats:" << std::endl;
    std::cout << "  Total Records: " << join_stats.total_records << std::endl;
    std::cout << "  Total Joins: " << join_stats.total_joins << std::endl;
    std::cout << "  Avg Join/Window: " << join_stats.avg_join_per_window << std::endl;
    std::cout << "  AQP Usage Count: " << join_stats.aqp_usage_count << std::endl;
    
    // ========== 9. 全局统计 ==========
    std::cout << "\n[9] Global statistics..." << std::endl;
    table_mgr.printTablesSummary();
    
    auto global_stats = table_mgr.getGlobalStats();
    std::cout << "\nGlobal Stats:" << std::endl;
    std::cout << "  Total Tables: " << global_stats.total_tables << std::endl;
    std::cout << "  Total Records: " << global_stats.total_records << std::endl;
    std::cout << "  Total Memory: " << global_stats.total_memory_bytes / 1024 << " KB" << std::endl;
    
    // ========== 10. 持久化 ==========
    std::cout << "\n[10] Flushing all tables..." << std::endl;
    table_mgr.flushAllTables();
    std::cout << "All tables flushed to disk." << std::endl;
    
    std::cout << "\n========== Demo Completed Successfully! ==========" << std::endl;
    
    return 0;
}
