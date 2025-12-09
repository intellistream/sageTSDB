#pragma once

#include "time_series_data.h"
#include "stream_table.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace sage_tsdb {

/**
 * @brief Join 结果表（用于 PECJ 输出数据）
 * 
 * 设计原则：
 * - 存储窗口级别的 Join 结果
 * - 支持精确结果和 AQP 估计值
 * - 支持 payload 序列化存储（完整 Join 记录）
 * - 支持按窗口查询和时间查询
 * 
 * 数据结构：
 * - window_id: 窗口标识符
 * - timestamp: 窗口结束时间戳
 * - join_count: 精确 Join 结果数
 * - aqp_estimate: AQP 估计值（可选）
 * - payload: 序列化的详细 Join 结果
 * - metrics: 计算指标（延迟、资源使用等）
 */
class JoinResultTable {
public:
    /**
     * @brief Join 结果记录
     */
    struct JoinRecord {
        // 基本信息
        uint64_t window_id;                        // 窗口 ID
        int64_t timestamp;                         // 窗口结束时间戳
        
        // Join 结果
        size_t join_count;                         // 精确 Join 数量
        double aqp_estimate;                       // AQP 估计值（-1 表示未启用）
        double selectivity;                        // Join 选择率
        
        // 详细结果（序列化存储）
        std::vector<uint8_t> payload;              // 完整 Join 结果
        
        // 计算指标
        struct ComputeMetrics {
            double computation_time_ms;            // 计算耗时
            size_t memory_used_bytes;              // 内存使用
            int threads_used;                      // 线程数
            double cpu_usage_percent;              // CPU 使用率
            bool used_aqp;                         // 是否使用 AQP
            std::string algorithm_type;            // 算法类型（如 "IAWJ", "PAWJ"）
        } metrics;
        
        // 元数据
        Tags tags;                                 // 可选标签（如 symbol, query_id）
        std::string error_message;                 // 错误信息（如有）
        
        JoinRecord()
            : window_id(0), timestamp(0), join_count(0), 
              aqp_estimate(-1.0), selectivity(0.0) {
            metrics.computation_time_ms = 0.0;
            metrics.memory_used_bytes = 0;
            metrics.threads_used = 0;
            metrics.cpu_usage_percent = 0.0;
            metrics.used_aqp = false;
        }
        
        /**
         * @brief 检查是否有错误
         */
        bool hasError() const { return !error_message.empty(); }
        
        /**
         * @brief 将 payload 反序列化为 Join 对
         */
        std::vector<std::pair<TimeSeriesData, TimeSeriesData>> deserializePayload() const;
        
        /**
         * @brief 从 Join 对序列化为 payload
         */
        void serializePayload(const std::vector<std::pair<TimeSeriesData, TimeSeriesData>>& join_pairs);
    };
    
    /**
     * @brief 构造函数
     * @param name 表名（默认 "join_results"）
     * @param config 表配置
     */
    explicit JoinResultTable(const std::string& name = "join_results",
                            const TableConfig& config = TableConfig{});
    
    ~JoinResultTable();
    
    // ========== 数据写入接口 ==========
    
    /**
     * @brief 插入 Join 结果
     * @param record Join 记录
     * @return 记录索引
     * 
     * 线程安全：支持多窗口并发写入
     */
    size_t insertJoinResult(const JoinRecord& record);
    
    /**
     * @brief 批量插入 Join 结果
     * @param records 记录列表
     * @return 每条记录的索引
     */
    std::vector<size_t> insertJoinResultBatch(const std::vector<JoinRecord>& records);
    
    /**
     * @brief 便捷方法：插入简单结果（仅统计）
     * @param window_id 窗口 ID
     * @param timestamp 时间戳
     * @param join_count Join 数量
     * @param metrics 计算指标
     * @return 记录索引
     */
    size_t insertSimpleResult(uint64_t window_id, 
                             int64_t timestamp,
                             size_t join_count,
                             const JoinRecord::ComputeMetrics& metrics);
    
    // ========== 数据查询接口 ==========
    
    /**
     * @brief 按窗口 ID 查询
     * @param window_id 窗口 ID
     * @return 该窗口的 Join 结果（可能为空）
     */
    std::vector<JoinRecord> queryByWindow(uint64_t window_id) const;
    
    /**
     * @brief 按时间范围查询
     * @param range 时间范围
     * @return 该时间范围内的所有窗口结果
     */
    std::vector<JoinRecord> queryByTimeRange(const TimeRange& range) const;
    
    /**
     * @brief 按标签查询
     * @param filter_tags 标签过滤器
     * @return 匹配的 Join 结果
     */
    std::vector<JoinRecord> queryByTags(const Tags& filter_tags) const;
    
    /**
     * @brief 查询最近 N 个窗口的结果
     * @param n 窗口数
     * @return 最近的 n 个窗口结果
     */
    std::vector<JoinRecord> queryLatest(size_t n) const;
    
    /**
     * @brief 聚合统计查询
     * @param range 时间范围
     * @return 聚合统计信息
     */
    struct AggregateStats {
        size_t total_windows;                      // 总窗口数
        size_t total_joins;                        // 总 Join 数
        double avg_join_count;                     // 平均 Join 数
        double avg_computation_time_ms;            // 平均计算时间
        double avg_selectivity;                    // 平均选择率
        size_t aqp_usage_count;                    // AQP 使用次数
        size_t error_count;                        // 错误窗口数
    };
    
    AggregateStats queryAggregateStats(const TimeRange& range) const;
    
    // ========== 索引管理 ==========
    
    /**
     * @brief 为 window_id 创建哈希索引（加速查询）
     */
    bool createWindowIndex();
    
    /**
     * @brief 为标签字段创建索引
     */
    bool createTagIndex(const std::string& tag_name);
    
    // ========== 表维护 ==========
    
    /**
     * @brief 删除旧的 Join 结果（数据清理）
     * @param before_timestamp 删除此时间之前的数据
     * @return 删除的记录数
     */
    size_t deleteOldResults(int64_t before_timestamp);
    
    /**
     * @brief 清空所有数据
     */
    void clear();
    
    // ========== 统计信息 ==========
    
    /**
     * @brief 获取表统计信息
     */
    struct Stats {
        std::string name;                          // 表名
        size_t total_records;                      // 总记录数
        size_t total_joins;                        // 总 Join 数
        double avg_join_per_window;                // 平均每窗口 Join 数
        double avg_computation_time_ms;            // 平均计算时间
        int64_t min_timestamp;                     // 最早时间戳
        int64_t max_timestamp;                     // 最晚时间戳
        size_t payload_size_bytes;                 // Payload 总大小
        size_t aqp_usage_count;                    // AQP 使用次数
        size_t error_count;                        // 错误窗口数
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
    std::string name_;                             // 表名
    TableConfig config_;                           // 表配置
    
    // 底层存储（复用 StreamTable 的 LSM-Tree）
    std::unique_ptr<StreamTable> storage_;         // 存储引擎
    
    // 窗口 ID 索引（加速查询）
    std::unordered_map<uint64_t, std::vector<size_t>> window_index_;
    
    // 统计信息
    mutable Stats stats_;
    
    // 线程安全
    mutable std::shared_mutex mutex_;
    
    // 内部辅助方法
    TimeSeriesData recordToTimeSeriesData(const JoinRecord& record) const;
    JoinRecord timeSeriesDataToRecord(const TimeSeriesData& data) const;
    void updateStats() const;
};

} // namespace sage_tsdb
