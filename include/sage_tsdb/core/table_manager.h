#pragma once

#include "stream_table.h"
#include "join_result_table.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <shared_mutex>

namespace sage_tsdb {

/**
 * @brief 表管理器（管理多个表的创建和访问）
 * 
 * 设计原则：
 * - 单一数据源：所有表统一管理
 * - 线程安全：支持并发创建和访问表
 * - 类型安全：区分 StreamTable 和 JoinResultTable
 * - 生命周期管理：自动清理不再使用的表
 * 
 * 使用场景：
 * - PECJ 需要创建 stream_s, stream_r 输入表
 * - PECJ 需要创建 join_results 输出表
 * - 支持多租户（多个查询独立的表命名空间）
 */
class TableManager {
public:
    /**
     * @brief 表类型
     */
    enum class TableType {
        Stream,        // 流输入表（StreamTable）
        JoinResult,    // Join 结果表（JoinResultTable）
        ComputeState   // 计算状态表（用于持久化 PECJ 状态）
    };
    
    /**
     * @brief 构造函数
     * @param base_data_dir 数据根目录
     */
    explicit TableManager(const std::string& base_data_dir = "");
    
    ~TableManager();
    
    // ========== 表创建接口 ==========
    
    /**
     * @brief 创建 Stream 表
     * @param name 表名（如 "stream_s", "stream_r"）
     * @param config 表配置
     * @return 是否创建成功
     * 
     * 约束：表名必须唯一
     */
    bool createStreamTable(const std::string& name,
                          const TableConfig& config = TableConfig{});
    
    /**
     * @brief 创建 Join 结果表
     * @param name 表名（默认 "join_results"）
     * @param config 表配置
     * @return 是否创建成功
     */
    bool createJoinResultTable(const std::string& name = "join_results",
                              const TableConfig& config = TableConfig{});
    
    /**
     * @brief 便捷方法：为 PECJ 创建标准表集合
     * @param prefix 表名前缀（如 "query1_"）
     * @return 是否全部创建成功
     * 
     * 创建：
     * - {prefix}stream_s
     * - {prefix}stream_r
     * - {prefix}join_results
     */
    bool createPECJTables(const std::string& prefix = "");
    
    // ========== 表访问接口 ==========
    
    /**
     * @brief 获取 Stream 表
     * @param name 表名
     * @return 表指针（不存在返回 nullptr）
     * 
     * 线程安全：返回的指针可在多线程中使用
     */
    std::shared_ptr<StreamTable> getStreamTable(const std::string& name);
    
    /**
     * @brief 获取 Join 结果表
     * @param name 表名
     * @return 表指针（不存在返回 nullptr）
     */
    std::shared_ptr<JoinResultTable> getJoinResultTable(const std::string& name);
    
    /**
     * @brief 检查表是否存在
     * @param name 表名
     * @return 是否存在
     */
    bool hasTable(const std::string& name) const;
    
    /**
     * @brief 获取表类型
     * @param name 表名
     * @return 表类型（不存在抛出异常）
     */
    TableType getTableType(const std::string& name) const;
    
    // ========== 表删除接口 ==========
    
    /**
     * @brief 删除表
     * @param name 表名
     * @return 是否删除成功
     * 
     * 警告：删除不可恢复，除非启用了持久化
     */
    bool dropTable(const std::string& name);
    
    /**
     * @brief 清空表数据（保留表结构）
     * @param name 表名
     * @return 是否清空成功
     */
    bool clearTable(const std::string& name);
    
    /**
     * @brief 删除所有表
     */
    void dropAllTables();
    
    // ========== 表列举接口 ==========
    
    /**
     * @brief 列出所有表名
     * @return 表名列表
     */
    std::vector<std::string> listTables() const;
    
    /**
     * @brief 列出指定类型的表
     * @param type 表类型
     * @return 表名列表
     */
    std::vector<std::string> listTablesByType(TableType type) const;
    
    /**
     * @brief 获取表数量
     */
    size_t getTableCount() const;
    
    // ========== 批量操作 ==========
    
    /**
     * @brief 批量插入数据到多个表
     * @param table_data 表名 → 数据列表的映射
     * @return 每个表的插入索引
     * 
     * 用途：同时写入 stream_s 和 stream_r
     */
    std::map<std::string, std::vector<size_t>> insertBatchToTables(
        const std::map<std::string, std::vector<TimeSeriesData>>& table_data);
    
    /**
     * @brief 批量查询多个表
     * @param queries 表名 → 查询范围的映射
     * @return 每个表的查询结果
     */
    std::map<std::string, std::vector<TimeSeriesData>> queryBatchFromTables(
        const std::map<std::string, TimeRange>& queries) const;
    
    // ========== 持久化管理 ==========
    
    /**
     * @brief 保存所有表到磁盘
     * @return 是否全部保存成功
     */
    bool saveAllTables();
    
    /**
     * @brief 从磁盘加载所有表
     * @return 是否全部加载成功
     */
    bool loadAllTables();
    
    /**
     * @brief 为表启用 checkpoint（定期持久化）
     * @param name 表名
     * @param interval_seconds checkpoint 间隔（秒）
     * @return 是否启用成功
     */
    bool enableCheckpoint(const std::string& name, int interval_seconds = 60);
    
    // ========== 统计信息 ==========
    
    /**
     * @brief 获取全局统计信息
     */
    struct GlobalStats {
        size_t total_tables;                       // 总表数
        size_t total_records;                      // 总记录数
        size_t total_memory_bytes;                 // 总内存占用
        size_t total_disk_bytes;                   // 总磁盘占用
        double total_write_throughput;             // 总写入吞吐量
        std::map<std::string, size_t> table_sizes; // 每个表的大小
    };
    
    GlobalStats getGlobalStats() const;
    
    /**
     * @brief 打印所有表的摘要信息
     */
    void printTablesSummary() const;
    
    // ========== 资源管理 ==========
    
    /**
     * @brief 触发所有表的 flush
     */
    void flushAllTables();
    
    /**
     * @brief 触发所有表的 compact
     */
    void compactAllTables();
    
    /**
     * @brief 设置全局内存限制
     * @param max_memory_bytes 最大内存（字节）
     * 
     * 超过限制时自动触发 flush
     */
    void setGlobalMemoryLimit(size_t max_memory_bytes);
    
    /**
     * @brief 获取当前内存使用
     */
    size_t getCurrentMemoryUsage() const;

private:
    // 内部表元数据
    struct TableMetadata {
        std::string name;                          // 表名
        TableType type;                            // 表类型
        std::shared_ptr<void> table_ptr;          // 表指针（类型擦除）
        std::chrono::steady_clock::time_point created_at; // 创建时间
        size_t access_count;                       // 访问计数
        
        TableMetadata(const std::string& n, TableType t, std::shared_ptr<void> p)
            : name(n), type(t), table_ptr(p), 
              created_at(std::chrono::steady_clock::now()), access_count(0) {}
    };
    
    std::string base_data_dir_;                    // 数据根目录
    
    // 表存储
    std::unordered_map<std::string, TableMetadata> tables_;
    
    // 全局配置
    size_t global_memory_limit_;                   // 全局内存限制
    
    // 线程安全
    mutable std::shared_mutex mutex_;
    
    // 内部辅助方法
    std::string getTableDataDir(const std::string& name) const;
    void checkMemoryLimit();
    void updateGlobalStats() const;
    
    // 类型转换辅助
    template<typename T>
    std::shared_ptr<T> getTableAs(const std::string& name, TableType expected_type);
};

// ========== 便捷的全局表访问接口 ==========

/**
 * @brief 全局表管理器单例（可选）
 * 
 * 使用场景：简化代码，避免到处传递 TableManager 指针
 */
class GlobalTableManager {
public:
    static TableManager& instance();
    
    // 禁止拷贝和移动
    GlobalTableManager(const GlobalTableManager&) = delete;
    GlobalTableManager& operator=(const GlobalTableManager&) = delete;
    
private:
    GlobalTableManager() = default;
    static TableManager* instance_;
};

// ========== 辅助宏（可选） ==========

// 快速访问全局表管理器
#define GET_TABLE_MANAGER() GlobalTableManager::instance()

// 快速获取表
#define GET_STREAM_TABLE(name) GET_TABLE_MANAGER().getStreamTable(name)
#define GET_JOIN_RESULT_TABLE(name) GET_TABLE_MANAGER().getJoinResultTable(name)

} // namespace sage_tsdb
