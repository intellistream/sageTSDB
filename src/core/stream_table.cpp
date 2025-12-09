#include "sage_tsdb/core/stream_table.h"
#include <algorithm>
#include <stdexcept>

namespace sage_tsdb {

StreamTable::StreamTable(const std::string& name, const TableConfig& config)
    : name_(name), config_(config) {
    
    // 初始化 MemTable
    memtable_ = std::make_unique<MemTable>(config_.memtable_size_bytes);
    
    // 初始化 LSM-Tree
    if (!config_.data_dir.empty()) {
        LSMConfig lsm_config;
        lsm_config.data_dir = config_.data_dir + "/" + name_;
        lsm_config.memtable_size_bytes = config_.memtable_size_bytes;
        lsm_tree_ = std::make_unique<LSMTree>(lsm_config);
    }
    
    // 初始化时间戳索引
    if (config_.enable_timestamp_index) {
        index_ = std::make_unique<TimeSeriesIndex>();
    }
    
    // 为配置的标签创建索引
    for (const auto& tag_name : config_.indexed_tags) {
        tag_indexes_[tag_name] = std::make_unique<TimeSeriesIndex>();
    }
    
    // 初始化统计信息
    stats_.name = name_;
    stats_.total_records = 0;
    stats_.memtable_records = 0;
    stats_.lsm_levels = 0;
    stats_.disk_size_bytes = 0;
    stats_.min_timestamp = std::numeric_limits<int64_t>::max();
    stats_.max_timestamp = std::numeric_limits<int64_t>::min();
    stats_.num_indexes = tag_indexes_.size() + (index_ ? 1 : 0);
    stats_.write_throughput = 0.0;
    stats_.query_latency_ms = 0.0;
    
    last_stats_update_ = std::chrono::steady_clock::now();
}

StreamTable::~StreamTable() {
    // 确保所有数据 flush 到磁盘
    if (lsm_tree_ && memtable_) {
        flush();
    }
}

size_t StreamTable::insert(const TimeSeriesData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 插入到 MemTable
    memtable_->put(data.timestamp, data);
    size_t index = stats_.total_records; // 使用计数作为索引
    
    // 更新索引
    if (index_) {
        index_->add(data);
    }
    
    // 更新标签索引
    for (const auto& [tag_name, tag_value] : data.tags) {
        if (tag_indexes_.count(tag_name)) {
            tag_indexes_[tag_name]->add(data);
        }
    }
    
    // 更新统计信息
    stats_.total_records++;
    stats_.memtable_records++;
    stats_.min_timestamp = std::min(stats_.min_timestamp, data.timestamp);
    stats_.max_timestamp = std::max(stats_.max_timestamp, data.timestamp);
    
    // 检查是否需要 flush
    maybeFlush();
    
    return index;
}

std::vector<size_t> StreamTable::insertBatch(const std::vector<TimeSeriesData>& data_list) {
    std::vector<size_t> indices;
    indices.reserve(data_list.size());
    
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    for (const auto& data : data_list) {
        memtable_->put(data.timestamp, data);
        size_t index = stats_.total_records;
        indices.push_back(index);
        
        // 更新索引
        if (index_) {
            index_->add(data);
        }
        
        for (const auto& [tag_name, tag_value] : data.tags) {
            if (tag_indexes_.count(tag_name)) {
                tag_indexes_[tag_name]->add(data);
            }
        }
        
        // 更新统计信息
        stats_.total_records++;
        stats_.min_timestamp = std::min(stats_.min_timestamp, data.timestamp);
        stats_.max_timestamp = std::max(stats_.max_timestamp, data.timestamp);
    }
    
    stats_.memtable_records += data_list.size();
    
    // 检查是否需要 flush
    maybeFlush();
    
    return indices;
}

std::vector<TimeSeriesData> StreamTable::query(const TimeRange& range,
                                               const Tags& filter_tags) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<TimeSeriesData> results;
    
    // 从 MemTable 查询
    if (memtable_) {
        auto mem_results = memtable_->range_query(range.start_time, range.end_time);
        // 应用标签过滤
        for (const auto& data : mem_results) {
            bool match = true;
            for (const auto& [key, value] : filter_tags) {
                if (data.tags.count(key) == 0 || data.tags.at(key) != value) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back(data);
            }
        }
    }
    
    // 从 Immutable MemTable 查询
    if (immutable_memtable_) {
        auto immut_results = immutable_memtable_->range_query(range.start_time, range.end_time);
        for (const auto& data : immut_results) {
            bool match = true;
            for (const auto& [key, value] : filter_tags) {
                if (data.tags.count(key) == 0 || data.tags.at(key) != value) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back(data);
            }
        }
    }
    
    // TODO: 从 LSM-Tree 查询（需要实现 LSMTree::range_query）
    
    // 合并去重，按时间排序
    std::sort(results.begin(), results.end(), 
             [](const TimeSeriesData& a, const TimeSeriesData& b) {
                 return a.timestamp < b.timestamp;
             });
    
    // 去重（如果同一数据在多个地方）
    results.erase(std::unique(results.begin(), results.end(),
                             [](const TimeSeriesData& a, const TimeSeriesData& b) {
                                 return a.timestamp == b.timestamp;
                             }),
                 results.end());
    
    return results;
}

std::vector<TimeSeriesData> StreamTable::queryWindow(uint64_t window_id) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // 查找窗口对应的时间范围
    if (window_ranges_.count(window_id) == 0) {
        // 窗口未注册，返回空
        return {};
    }
    
    const TimeRange& range = window_ranges_[window_id];
    lock.unlock(); // 释放锁，避免死锁
    
    return query(range);
}

std::vector<TimeSeriesData> StreamTable::queryLatest(size_t n) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<TimeSeriesData> results;
    
    // 从 MemTable 获取最新数据（通常已按时间排序）
    if (memtable_) {
        const auto& mem_data = memtable_->get_all();
        for (const auto& [ts, data] : mem_data) {
            results.push_back(data);
        }
    }
    
    // 按时间降序排序
    std::sort(results.begin(), results.end(),
             [](const TimeSeriesData& a, const TimeSeriesData& b) {
                 return a.timestamp > b.timestamp;
             });
    
    // 返回前 n 条
    if (results.size() > n) {
        results.resize(n);
    }
    
    return results;
}

size_t StreamTable::count(const TimeRange& range) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    size_t total = 0;
    
    // 从 MemTable 统计
    if (memtable_) {
        auto mem_results = memtable_->range_query(range.start_time, range.end_time);
        total += mem_results.size();
    }
    
    // 从 Immutable MemTable 统计
    if (immutable_memtable_) {
        auto immut_results = immutable_memtable_->range_query(range.start_time, range.end_time);
        total += immut_results.size();
    }
    
    // TODO: 从 LSM-Tree 统计
    
    return total;
}

bool StreamTable::createIndex(const std::string& field_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (tag_indexes_.count(field_name)) {
        return false; // 索引已存在
    }
    
    tag_indexes_[field_name] = std::make_unique<TimeSeriesIndex>();
    stats_.num_indexes++;
    
    return true;
}

bool StreamTable::dropIndex(const std::string& field_name) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = tag_indexes_.find(field_name);
    if (it == tag_indexes_.end()) {
        return false; // 索引不存在
    }
    
    tag_indexes_.erase(it);
    stats_.num_indexes--;
    
    return true;
}

std::vector<std::string> StreamTable::listIndexes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> index_names;
    if (index_) {
        index_names.push_back("timestamp");
    }
    
    for (const auto& [name, _] : tag_indexes_) {
        index_names.push_back(name);
    }
    
    return index_names;
}

bool StreamTable::flush() {
    std::lock_guard<std::mutex> flush_lock(flush_mutex_);
    
    if (!memtable_ || memtable_->size() == 0) {
        return true; // 没有数据需要 flush
    }
    
    doFlush();
    return true;
}

bool StreamTable::compact() {
    if (lsm_tree_) {
        return lsm_tree_->flush(); // LSMTree uses flush() for compaction
    }
    return false;
}

void StreamTable::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 清空 MemTable
    if (memtable_) {
        memtable_->clear();
    }
    
    if (immutable_memtable_) {
        immutable_memtable_.reset();
    }
    
    // TODO: 清空 LSM-Tree (需要实现 clear())
    
    // 清空索引
    if (index_) {
        index_->clear();
    }
    
    for (auto& [_, idx] : tag_indexes_) {
        idx->clear();
    }
    
    // 重置统计信息
    stats_.total_records = 0;
    stats_.memtable_records = 0;
    stats_.min_timestamp = std::numeric_limits<int64_t>::max();
    stats_.max_timestamp = std::numeric_limits<int64_t>::min();
}

StreamTable::Stats StreamTable::getStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    updateStats();
    return stats_;
}

size_t StreamTable::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_.total_records;
}

bool StreamTable::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_.total_records == 0;
}

// ========== 内部辅助方法 ==========

void StreamTable::maybeFlush() {
    if (!memtable_ || !lsm_tree_) {
        return;
    }
    
    // 检查 MemTable 是否需要 flush
    size_t mem_size = memtable_->size();
    size_t threshold = config_.memtable_size_bytes * config_.memtable_flush_threshold;
    
    if (mem_size >= threshold) {
        // 异步触发 flush（不阻塞写入）
        std::thread([this]() {
            this->flush();
        }).detach();
    }
}

void StreamTable::doFlush() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (!memtable_ || memtable_->size() == 0) {
        return;
    }
    
    // 将当前 MemTable 标记为 Immutable
    immutable_memtable_ = std::move(memtable_);
    
    // 创建新的 MemTable
    memtable_ = std::make_unique<MemTable>(config_.memtable_size_bytes);
    
    // 更新统计
    stats_.memtable_records = 0;
    
    lock.unlock(); // 释放锁，允许写入新 MemTable
    
    // TODO: 将 Immutable MemTable flush 到 LSM-Tree（需要适配 API）
    if (lsm_tree_ && immutable_memtable_) {
        // LSMTree::flush() doesn't take parameter, need to redesign
        // For now, just clear the immutable memtable
        std::unique_lock<std::shared_mutex> lock2(mutex_);
        immutable_memtable_.reset();
    }
}

void StreamTable::updateStats() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_stats_update_).count();
    
    if (elapsed < 1) {
        return; // 不频繁更新
    }
    
    // TODO: 更新 LSM-Tree 统计 (需要实现 API)
    
    // 更新 MemTable 统计
    if (memtable_) {
        stats_.memtable_records = memtable_->size();
    }
    
    last_stats_update_ = now;
}

} // namespace sage_tsdb
