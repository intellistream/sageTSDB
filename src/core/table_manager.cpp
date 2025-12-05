#include "sage_tsdb/core/table_manager.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>

namespace sage_tsdb {

TableManager::TableManager(const std::string& base_data_dir)
    : base_data_dir_(base_data_dir),
      global_memory_limit_(0) {
}

TableManager::~TableManager() {
    // 保存所有表到磁盘
    saveAllTables();
}

bool TableManager::createStreamTable(const std::string& name, const TableConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (tables_.count(name)) {
        return false; // 表已存在
    }
    
    // 设置数据目录
    TableConfig table_config = config;
    if (table_config.data_dir.empty() && !base_data_dir_.empty()) {
        table_config.data_dir = getTableDataDir(name);
    }
    
    // 创建表
    auto table = std::make_shared<StreamTable>(name, table_config);
    
    // 注册表
    tables_.emplace(name, TableMetadata(name, TableType::Stream, table));
    
    return true;
}

bool TableManager::createJoinResultTable(const std::string& name, const TableConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (tables_.count(name)) {
        return false; // 表已存在
    }
    
    // 设置数据目录
    TableConfig table_config = config;
    if (table_config.data_dir.empty() && !base_data_dir_.empty()) {
        table_config.data_dir = getTableDataDir(name);
    }
    
    // 创建表
    auto table = std::make_shared<JoinResultTable>(name, table_config);
    
    // 注册表
    tables_.emplace(name, TableMetadata(name, TableType::JoinResult, table));
    
    return true;
}

bool TableManager::createPECJTables(const std::string& prefix) {
    bool success = true;
    
    // 创建 Stream S 表
    std::string stream_s_name = prefix + "stream_s";
    success &= createStreamTable(stream_s_name);
    
    // 创建 Stream R 表
    std::string stream_r_name = prefix + "stream_r";
    success &= createStreamTable(stream_r_name);
    
    // 创建 Join 结果表
    std::string join_result_name = prefix + "join_results";
    success &= createJoinResultTable(join_result_name);
    
    return success;
}

std::shared_ptr<StreamTable> TableManager::getStreamTable(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return nullptr; // 表不存在
    }
    
    if (it->second.type != TableType::Stream) {
        throw std::runtime_error("Table '" + name + "' is not a StreamTable");
    }
    
    // 更新访问计数
    it->second.access_count++;
    
    return std::static_pointer_cast<StreamTable>(it->second.table_ptr);
}

std::shared_ptr<JoinResultTable> TableManager::getJoinResultTable(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return nullptr; // 表不存在
    }
    
    if (it->second.type != TableType::JoinResult) {
        throw std::runtime_error("Table '" + name + "' is not a JoinResultTable");
    }
    
    // 更新访问计数
    it->second.access_count++;
    
    return std::static_pointer_cast<JoinResultTable>(it->second.table_ptr);
}

bool TableManager::hasTable(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tables_.count(name) > 0;
}

TableManager::TableType TableManager::getTableType(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        throw std::runtime_error("Table '" + name + "' does not exist");
    }
    
    return it->second.type;
}

bool TableManager::dropTable(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return false; // 表不存在
    }
    
    tables_.erase(it);
    return true;
}

bool TableManager::clearTable(const std::string& name) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return false; // 表不存在
    }
    
    if (it->second.type == TableType::Stream) {
        auto table = std::static_pointer_cast<StreamTable>(it->second.table_ptr);
        table->clear();
    } else if (it->second.type == TableType::JoinResult) {
        auto table = std::static_pointer_cast<JoinResultTable>(it->second.table_ptr);
        table->clear();
    }
    
    return true;
}

void TableManager::dropAllTables() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    tables_.clear();
}

std::vector<std::string> TableManager::listTables() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> table_names;
    table_names.reserve(tables_.size());
    
    for (const auto& [name, _] : tables_) {
        table_names.push_back(name);
    }
    
    return table_names;
}

std::vector<std::string> TableManager::listTablesByType(TableType type) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> table_names;
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == type) {
            table_names.push_back(name);
        }
    }
    
    return table_names;
}

size_t TableManager::getTableCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return tables_.size();
}

std::map<std::string, std::vector<size_t>> TableManager::insertBatchToTables(
    const std::map<std::string, std::vector<TimeSeriesData>>& table_data) {
    
    std::map<std::string, std::vector<size_t>> result;
    
    for (const auto& [table_name, data_list] : table_data) {
        auto table = getStreamTable(table_name);
        if (table) {
            result[table_name] = table->insertBatch(data_list);
        }
    }
    
    checkMemoryLimit();
    
    return result;
}

std::map<std::string, std::vector<TimeSeriesData>> TableManager::queryBatchFromTables(
    const std::map<std::string, TimeRange>& queries) const {
    
    std::map<std::string, std::vector<TimeSeriesData>> result;
    
    for (const auto& [table_name, range] : queries) {
        // 尝试作为 StreamTable 查询
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = tables_.find(table_name);
        if (it == tables_.end()) {
            continue;
        }
        
        if (it->second.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(it->second.table_ptr);
            lock.unlock();
            result[table_name] = table->query(range);
        }
    }
    
    return result;
}

bool TableManager::saveAllTables() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    bool success = true;
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            success &= table->flush();
        } else if (metadata.type == TableType::JoinResult) {
            auto table = std::static_pointer_cast<JoinResultTable>(metadata.table_ptr);
            // JoinResultTable 使用 StreamTable 作为底层存储，会自动 flush
        }
    }
    
    return success;
}

bool TableManager::loadAllTables() {
    // TODO: 实现从磁盘加载表的逻辑
    // 需要扫描数据目录，识别表类型，重建表对象
    return false;
}

bool TableManager::enableCheckpoint(const std::string& name, int interval_seconds) {
    // TODO: 实现定期 checkpoint 逻辑
    // 可以启动后台线程定期调用 flush
    return false;
}

TableManager::GlobalStats TableManager::getGlobalStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    GlobalStats stats;
    stats.total_tables = tables_.size();
    stats.total_records = 0;
    stats.total_memory_bytes = 0;
    stats.total_disk_bytes = 0;
    stats.total_write_throughput = 0.0;
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            auto table_stats = table->getStats();
            
            stats.total_records += table_stats.total_records;
            stats.total_disk_bytes += table_stats.disk_size_bytes;
            stats.total_write_throughput += table_stats.write_throughput;
            stats.table_sizes[name] = table_stats.total_records;
            
        } else if (metadata.type == TableType::JoinResult) {
            auto table = std::static_pointer_cast<JoinResultTable>(metadata.table_ptr);
            auto table_stats = table->getStats();
            
            stats.total_records += table_stats.total_records;
            stats.table_sizes[name] = table_stats.total_records;
        }
    }
    
    return stats;
}

void TableManager::printTablesSummary() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::cout << "\n========== Table Manager Summary ==========" << std::endl;
    std::cout << "Total Tables: " << tables_.size() << std::endl;
    std::cout << "Data Directory: " << base_data_dir_ << std::endl;
    std::cout << "\n" << std::left << std::setw(25) << "Table Name"
              << std::setw(15) << "Type"
              << std::setw(15) << "Records"
              << std::setw(15) << "Accesses"
              << std::endl;
    std::cout << std::string(70, '-') << std::endl;
    
    for (const auto& [name, metadata] : tables_) {
        std::string type_str;
        size_t records = 0;
        
        if (metadata.type == TableType::Stream) {
            type_str = "Stream";
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            records = table->size();
        } else if (metadata.type == TableType::JoinResult) {
            type_str = "JoinResult";
            auto table = std::static_pointer_cast<JoinResultTable>(metadata.table_ptr);
            records = table->size();
        } else {
            type_str = "Unknown";
        }
        
        std::cout << std::left << std::setw(25) << name
                  << std::setw(15) << type_str
                  << std::setw(15) << records
                  << std::setw(15) << metadata.access_count
                  << std::endl;
    }
    
    std::cout << "==========================================" << std::endl;
}

void TableManager::flushAllTables() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            table->flush();
        }
    }
}

void TableManager::compactAllTables() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            table->compact();
        }
    }
}

void TableManager::setGlobalMemoryLimit(size_t max_memory_bytes) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    global_memory_limit_ = max_memory_bytes;
}

size_t TableManager::getCurrentMemoryUsage() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t total = 0;
    
    for (const auto& [name, metadata] : tables_) {
        if (metadata.type == TableType::Stream) {
            auto table = std::static_pointer_cast<StreamTable>(metadata.table_ptr);
            auto stats = table->getStats();
            // 假设 MemTable 占用约为记录数 * 1KB（粗略估计）
            total += stats.memtable_records * 1024;
        }
    }
    
    return total;
}

// ========== 内部辅助方法 ==========

std::string TableManager::getTableDataDir(const std::string& name) const {
    return base_data_dir_ + "/" + name;
}

void TableManager::checkMemoryLimit() {
    if (global_memory_limit_ == 0) {
        return; // 未设置限制
    }
    
    size_t current_usage = getCurrentMemoryUsage();
    
    if (current_usage > global_memory_limit_) {
        // 触发全局 flush
        flushAllTables();
    }
}

// ========== GlobalTableManager 实现 ==========

TableManager* GlobalTableManager::instance_ = nullptr;

TableManager& GlobalTableManager::instance() {
    static TableManager manager;
    return manager;
}

} // namespace sage_tsdb
