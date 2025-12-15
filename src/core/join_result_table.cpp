#include "sage_tsdb/core/join_result_table.h"
#include <algorithm>
#include <stdexcept>
#include <sstream>

namespace sage_tsdb {

// ========== JoinRecord 序列化方法 ==========

std::vector<std::pair<TimeSeriesData, TimeSeriesData>> 
JoinResultTable::JoinRecord::deserializePayload() const {
    // TODO: 实现反序列化逻辑
    // 这里需要根据 payload 格式解析 Join 对
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> result;
    
    // 简单实现：假设 payload 为空表示无详细结果
    if (payload.empty()) {
        return result;
    }
    
    // 实际实现需要使用序列化库（如 protobuf, msgpack）
    // 这里仅作占位
    
    return result;
}

void JoinResultTable::JoinRecord::serializePayload(
    const std::vector<std::pair<TimeSeriesData, TimeSeriesData>>& join_pairs) {
    // TODO: 实现序列化逻辑
    // 将 Join 对序列化为字节流
    
    // 简单实现：如果为空则不存储
    if (join_pairs.empty()) {
        payload.clear();
        return;
    }
    
    // 实际实现需要使用序列化库
    // 这里仅作占位
}

// ========== JoinResultTable 实现 ==========

JoinResultTable::JoinResultTable(const std::string& name, const TableConfig& config)
    : name_(name), config_(config) {
    
    // 使用 StreamTable 作为底层存储
    storage_ = std::make_unique<StreamTable>(name + "_storage", config);
    
    // 初始化统计信息
    stats_.name = name_;
    stats_.total_records = 0;
    stats_.total_joins = 0;
    stats_.avg_join_per_window = 0.0;
    stats_.avg_computation_time_ms = 0.0;
    stats_.min_timestamp = std::numeric_limits<int64_t>::max();
    stats_.max_timestamp = std::numeric_limits<int64_t>::min();
    stats_.payload_size_bytes = 0;
    stats_.aqp_usage_count = 0;
    stats_.error_count = 0;
}

JoinResultTable::~JoinResultTable() {
    // 确保所有数据持久化
    if (storage_) {
        storage_->flush();
    }
}

size_t JoinResultTable::insertJoinResult(const JoinRecord& record) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // 将 JoinRecord 转换为 TimeSeriesData
    TimeSeriesData data = recordToTimeSeriesData(record);
    
    // 插入到底层存储
    size_t index = storage_->insert(data);
    
    // 更新窗口索引
    window_index_[record.window_id].push_back(index);
    
    // 更新统计信息
    stats_.total_records++;
    stats_.total_joins += record.join_count;
    stats_.min_timestamp = std::min(stats_.min_timestamp, record.timestamp);
    stats_.max_timestamp = std::max(stats_.max_timestamp, record.timestamp);
    stats_.payload_size_bytes += record.payload.size();
    
    if (record.metrics.used_aqp) {
        stats_.aqp_usage_count++;
    }
    
    if (record.hasError()) {
        stats_.error_count++;
    }
    
    // 更新平均值
    if (stats_.total_records > 0) {
        stats_.avg_join_per_window = 
            static_cast<double>(stats_.total_joins) / stats_.total_records;
        stats_.avg_computation_time_ms = 
            (stats_.avg_computation_time_ms * (stats_.total_records - 1) + 
             record.metrics.computation_time_ms) / stats_.total_records;
    }
    
    return index;
}

std::vector<size_t> JoinResultTable::insertJoinResultBatch(
    const std::vector<JoinRecord>& records) {
    std::vector<size_t> indices;
    indices.reserve(records.size());
    
    for (const auto& record : records) {
        indices.push_back(insertJoinResult(record));
    }
    
    return indices;
}

size_t JoinResultTable::insertSimpleResult(
    uint64_t window_id,
    int64_t timestamp,
    size_t join_count,
    const JoinRecord::ComputeMetrics& metrics) {
    
    JoinRecord record;
    record.window_id = window_id;
    record.timestamp = timestamp;
    record.join_count = join_count;
    record.metrics = metrics;
    
    return insertJoinResult(record);
}

std::vector<JoinResultTable::JoinRecord> 
JoinResultTable::queryByWindow(uint64_t window_id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<JoinRecord> results;
    
    // 查找窗口索引
    auto it = window_index_.find(window_id);
    if (it == window_index_.end()) {
        return results; // 窗口不存在
    }
    
    // 通过索引查询数据
    for (size_t idx : it->second) {
        // 这里需要实现通过索引查询单条记录的方法
        // 暂时使用查询所有数据然后过滤的方式
        // TODO: 优化为直接索引访问
    }
    
    // 临时实现：查询所有数据并过滤
    Tags filter_tags = {{"window_id", std::to_string(window_id)}};
    TimeRange full_range(0, std::numeric_limits<int64_t>::max());
    auto data_list = storage_->query(full_range, filter_tags);
    
    for (const auto& data : data_list) {
        results.push_back(timeSeriesDataToRecord(data));
    }
    
    return results;
}

std::vector<JoinResultTable::JoinRecord> 
JoinResultTable::queryByTimeRange(const TimeRange& range) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto data_list = storage_->query(range);
    
    std::vector<JoinRecord> results;
    results.reserve(data_list.size());
    
    for (const auto& data : data_list) {
        results.push_back(timeSeriesDataToRecord(data));
    }
    
    return results;
}

std::vector<JoinResultTable::JoinRecord> 
JoinResultTable::queryByTags(const Tags& filter_tags) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    TimeRange full_range(0, std::numeric_limits<int64_t>::max());
    auto data_list = storage_->query(full_range, filter_tags);
    
    std::vector<JoinRecord> results;
    results.reserve(data_list.size());
    
    for (const auto& data : data_list) {
        results.push_back(timeSeriesDataToRecord(data));
    }
    
    return results;
}

std::vector<JoinResultTable::JoinRecord> 
JoinResultTable::queryLatest(size_t n) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto data_list = storage_->queryLatest(n);
    
    std::vector<JoinRecord> results;
    results.reserve(data_list.size());
    
    for (const auto& data : data_list) {
        results.push_back(timeSeriesDataToRecord(data));
    }
    
    return results;
}

JoinResultTable::AggregateStats 
JoinResultTable::queryAggregateStats(const TimeRange& range) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto records = queryByTimeRange(range);
    
    AggregateStats agg_stats;
    agg_stats.total_windows = records.size();
    agg_stats.total_joins = 0;
    agg_stats.avg_join_count = 0.0;
    agg_stats.avg_computation_time_ms = 0.0;
    agg_stats.avg_selectivity = 0.0;
    agg_stats.aqp_usage_count = 0;
    agg_stats.error_count = 0;
    
    if (records.empty()) {
        return agg_stats;
    }
    
    double total_computation_time = 0.0;
    double total_selectivity = 0.0;
    
    for (const auto& record : records) {
        agg_stats.total_joins += record.join_count;
        total_computation_time += record.metrics.computation_time_ms;
        total_selectivity += record.selectivity;
        
        if (record.metrics.used_aqp) {
            agg_stats.aqp_usage_count++;
        }
        
        if (record.hasError()) {
            agg_stats.error_count++;
        }
    }
    
    agg_stats.avg_join_count = 
        static_cast<double>(agg_stats.total_joins) / records.size();
    agg_stats.avg_computation_time_ms = total_computation_time / records.size();
    agg_stats.avg_selectivity = total_selectivity / records.size();
    
    return agg_stats;
}

bool JoinResultTable::createWindowIndex() {
    // 窗口索引在插入时自动维护，这里无需额外操作
    return true;
}

bool JoinResultTable::createTagIndex(const std::string& tag_name) {
    return storage_->createIndex(tag_name);
}

size_t JoinResultTable::deleteOldResults(int64_t before_timestamp) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    // TODO: 实现删除逻辑
    // 需要扩展 StreamTable 支持删除操作
    
    return 0;
}

void JoinResultTable::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    storage_->clear();
    window_index_.clear();
    
    // 重置统计信息
    stats_.total_records = 0;
    stats_.total_joins = 0;
    stats_.avg_join_per_window = 0.0;
    stats_.avg_computation_time_ms = 0.0;
    stats_.min_timestamp = std::numeric_limits<int64_t>::max();
    stats_.max_timestamp = std::numeric_limits<int64_t>::min();
    stats_.payload_size_bytes = 0;
    stats_.aqp_usage_count = 0;
    stats_.error_count = 0;
}

JoinResultTable::Stats JoinResultTable::getStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    updateStats();
    return stats_;
}

size_t JoinResultTable::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_.total_records;
}

bool JoinResultTable::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return stats_.total_records == 0;
}

// ========== 内部辅助方法 ==========

TimeSeriesData JoinResultTable::recordToTimeSeriesData(const JoinRecord& record) const {
    TimeSeriesData data;
    data.timestamp = record.timestamp;
    data.value = static_cast<double>(record.join_count);
    
    // 将 JoinRecord 字段编码到 tags 和 fields
    data.tags["window_id"] = std::to_string(record.window_id);
    data.tags["algorithm"] = record.metrics.algorithm_type;
    
    data.fields["join_count"] = std::to_string(record.join_count);
    data.fields["aqp_estimate"] = std::to_string(record.aqp_estimate);
    data.fields["selectivity"] = std::to_string(record.selectivity);
    data.fields["computation_time_ms"] = std::to_string(record.metrics.computation_time_ms);
    data.fields["memory_used_bytes"] = std::to_string(record.metrics.memory_used_bytes);
    data.fields["threads_used"] = std::to_string(record.metrics.threads_used);
    data.fields["cpu_usage_percent"] = std::to_string(record.metrics.cpu_usage_percent);
    data.fields["used_aqp"] = record.metrics.used_aqp ? "true" : "false";
    
    if (!record.error_message.empty()) {
        data.fields["error"] = record.error_message;
    }
    
    // 合并用户自定义标签
    for (const auto& [k, v] : record.tags) {
        data.tags[k] = v;
    }
    
    // Payload 存储（需要特殊处理）
    // TODO: 考虑使用专门的 Blob 存储
    
    return data;
}

JoinResultTable::JoinRecord 
JoinResultTable::timeSeriesDataToRecord(const TimeSeriesData& data) const {
    JoinRecord record;
    
    // 从 tags 和 fields 解码
    if (data.tags.count("window_id")) {
        record.window_id = std::stoull(data.tags.at("window_id"));
    }
    
    if (data.tags.count("algorithm")) {
        record.metrics.algorithm_type = data.tags.at("algorithm");
    }
    
    record.timestamp = data.timestamp;
    
    if (data.fields.count("join_count")) {
        record.join_count = std::stoull(data.fields.at("join_count"));
    }
    
    if (data.fields.count("aqp_estimate")) {
        record.aqp_estimate = std::stod(data.fields.at("aqp_estimate"));
    }
    
    if (data.fields.count("selectivity")) {
        record.selectivity = std::stod(data.fields.at("selectivity"));
    }
    
    if (data.fields.count("computation_time_ms")) {
        record.metrics.computation_time_ms = std::stod(data.fields.at("computation_time_ms"));
    }
    
    if (data.fields.count("memory_used_bytes")) {
        record.metrics.memory_used_bytes = std::stoull(data.fields.at("memory_used_bytes"));
    }
    
    if (data.fields.count("threads_used")) {
        record.metrics.threads_used = std::stoi(data.fields.at("threads_used"));
    }
    
    if (data.fields.count("cpu_usage_percent")) {
        record.metrics.cpu_usage_percent = std::stod(data.fields.at("cpu_usage_percent"));
    }
    
    if (data.fields.count("used_aqp")) {
        record.metrics.used_aqp = (data.fields.at("used_aqp") == "true");
    }
    
    if (data.fields.count("error")) {
        record.error_message = data.fields.at("error");
    }
    
    // 恢复用户自定义标签
    for (const auto& [k, v] : data.tags) {
        if (k != "window_id" && k != "algorithm") {
            record.tags[k] = v;
        }
    }
    
    return record;
}

void JoinResultTable::updateStats() const {
    // 统计信息在插入时已更新，这里无需额外操作
}

} // namespace sage_tsdb
