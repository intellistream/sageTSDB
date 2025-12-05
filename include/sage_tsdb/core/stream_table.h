#pragma once

#include "time_series_data.h"
#include "time_series_index.h"
#include "lsm_tree.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>

namespace sage_tsdb {

/**
 * @brief 表配置
 */
struct TableConfig {
    // MemTable 配置
    size_t memtable_size_bytes = 64 * 1024 * 1024;  // 64MB
    double memtable_flush_threshold = 0.9;           // 90% 触发 flush
    
    // LSM-Tree 配置
    size_t lsm_level0_file_num_compaction_trigger = 4;
    size_t lsm_max_levels = 7;
    double lsm_level_size_multiplier = 10.0;
    
    // 索引配置
    bool enable_timestamp_index = true;
    std::vector<std::string> indexed_tags;           // 需要索引的标签
    
    // 性能配置
    size_t write_buffer_size = 4 * 1024 * 1024;     // 4MB
    bool enable_compression = true;
    
    // 持久化配置
    std::string data_dir;                            // 数据目录
    bool enable_wal = true;                          // 写前日志
};

/**
 * @brief Stream 表（用于 PECJ 输入数据）
 * 
 * 设计原则：
 * - 所有数据先写入 MemTable，再 flush 到 LSM-Tree
 * - 支持乱序插入（自动按时间排序）
 * - 提供窗口查询接口（高效范围查询）
 * - 支持标签索引（加速过滤）
 * 
 * 适用场景：
 * - Stream S 和 Stream R 的独立表
 * - 高吞吐量写入 (> 100K events/sec)
 * - 低延迟窗口查询 (< 10ms)
 */
class StreamTable {
public:
    /**
     * @brief 构造函数
     * @param name 表名（如 "stream_s", "stream_r"）
     * @param config 表配置
     */
    explicit StreamTable(const std::string& name,
                        const TableConfig& config = TableConfig{});
    
    ~StreamTable();
    
    // ========== 数据写入接口 ==========
    
    /**
     * @brief 插入单条数据
     * @param data 时间序列数据
     * @return 数据在表中的索引
     * 
     * 线程安全：支持多线程并发写入
     * 性能：O(log n) 平均，O(1) 最好（直接插入 MemTable）
     */
    size_t insert(const TimeSeriesData& data);
    
    /**
     * @brief 批量插入数据
     * @param data_list 数据列表
     * @return 每条数据的索引
     * 
     * 优化：批量写入减少锁竞争
     */
    std::vector<size_t> insertBatch(const std::vector<TimeSeriesData>& data_list);
    
    // ========== 数据查询接口 ==========
    
    /**
     * @brief 查询指定时间范围内的数据
     * @param range 时间范围 [start, end]（inclusive）
     * @param filter_tags 可选的标签过滤器
     * @return 匹配的数据列表（已按时间排序）
     * 
     * 实现：
     * 1. 查询 MemTable（内存中的最新数据）
     * 2. 查询 Immutable MemTable（正在 flush 的数据）
     * 3. 查询 LSM-Tree 各层（磁盘上的历史数据）
     * 4. 合并去重，按时间排序返回
     */
    std::vector<TimeSeriesData> query(const TimeRange& range,
                                      const Tags& filter_tags = {}) const;
    
    /**
     * @brief 查询指定窗口的数据（窗口语义支持）
     * @param window_id 窗口标识符
     * @return 该窗口内的所有数据
     * 
     * 窗口计算：
     * - window_id → [start_time, end_time) 的映射由 WindowScheduler 维护
     * - 这里只是便捷接口，底层调用 query(TimeRange)
     */
    std::vector<TimeSeriesData> queryWindow(uint64_t window_id);
    
    /**
     * @brief 查询最新的 N 条数据
     * @param n 数据条数
     * @return 最新的 n 条数据（降序排列）
     */
    std::vector<TimeSeriesData> queryLatest(size_t n) const;
    
    /**
     * @brief 统计查询（不返回完整数据）
     * @param range 时间范围
     * @return 数据条数
     * 
     * 优化：仅统计，不加载完整数据
     */
    size_t count(const TimeRange& range) const;
    
    // ========== 索引管理 ==========
    
    /**
     * @brief 为指定字段创建索引
     * @param field_name 字段名（如 "symbol", "key"）
     * @return 是否创建成功
     * 
     * 用途：加速带标签过滤的查询
     */
    bool createIndex(const std::string& field_name);
    
    /**
     * @brief 删除索引
     */
    bool dropIndex(const std::string& field_name);
    
    /**
     * @brief 列出所有索引
     */
    std::vector<std::string> listIndexes() const;
    
    // ========== 表维护 ==========
    
    /**
     * @brief 手动触发 MemTable flush
     * @return 是否成功
     * 
     * 正常情况下自动触发，但可手动调用（如测试、checkpoint）
     */
    bool flush();
    
    /**
     * @brief 触发 LSM-Tree 压缩
     */
    bool compact();
    
    /**
     * @brief 清空所有数据
     */
    void clear();
    
    // ========== 统计信息 ==========
    
    /**
     * @brief 获取表统计信息
     */
    struct Stats {
        std::string name;                // 表名
        size_t total_records;            // 总记录数
        size_t memtable_records;         // MemTable 记录数
        size_t lsm_levels;               // LSM-Tree 层数
        size_t disk_size_bytes;          // 磁盘占用
        int64_t min_timestamp;           // 最早时间戳
        int64_t max_timestamp;           // 最晚时间戳
        size_t num_indexes;              // 索引数量
        double write_throughput;         // 写入吞吐量 (records/sec)
        double query_latency_ms;         // 平均查询延迟 (ms)
    };
    
    Stats getStats() const;
    
    /**
     * @brief 获取表名
     */
    const std::string& getName() const { return name_; }
    
    /**
     * @brief 获取表大小（记录数）
     */
    size_t size() const;
    
    /**
     * @brief 检查表是否为空
     */
    bool empty() const;

private:
    std::string name_;                              // 表名
    TableConfig config_;                            // 表配置 (forward declared above)
    
    // LSM-Tree 存储引擎
    std::unique_ptr<MemTable> memtable_;           // 当前活跃的 MemTable
    std::unique_ptr<MemTable> immutable_memtable_; // 正在 flush 的 MemTable
    std::unique_ptr<LSMTree> lsm_tree_;            // LSM-Tree（Level 0-N）
    
    // 索引
    std::unique_ptr<TimeSeriesIndex> index_;       // 时间戳索引
    std::unordered_map<std::string, 
                      std::unique_ptr<TimeSeriesIndex>> tag_indexes_; // 标签索引
    
    // 窗口映射（可选，由 WindowScheduler 管理）
    mutable std::unordered_map<uint64_t, TimeRange> window_ranges_;
    
    // 统计信息
    mutable Stats stats_;
    mutable std::chrono::steady_clock::time_point last_stats_update_;
    
    // 线程安全
    mutable std::shared_mutex mutex_;              // 读写锁
    std::mutex flush_mutex_;                        // flush 操作锁
    
    // 内部辅助方法
    void maybeFlush();                             // 检查是否需要 flush
    void doFlush();                                // 执行 flush
    void updateStats() const;                      // 更新统计信息
    std::vector<TimeSeriesData> mergeQueryResults(
        const std::vector<TimeSeriesData>& mem_results,
        const std::vector<TimeSeriesData>& lsm_results) const;
};

} // namespace sage_tsdb
