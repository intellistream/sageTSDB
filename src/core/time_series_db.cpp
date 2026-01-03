#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/storage_engine.h"
#include "sage_tsdb/algorithms/algorithm_base.h"
#include "sage_tsdb/core/resource_manager.h"
#include <stdexcept>

namespace sage_tsdb {

TimeSeriesDB::TimeSeriesDB()
    : index_(std::make_unique<TimeSeriesIndex>()),
      query_count_(0),
      write_count_(0),
      storage_engine_(std::make_unique<StorageEngine>()),
      resource_manager_(nullptr) {}

TimeSeriesDB::~TimeSeriesDB() = default;

// ========== Multi-Table Management Implementation ==========

bool TimeSeriesDB::createTable(const std::string& name, TableType type) {
    // Check if table already exists
    if (tables_.find(name) != tables_.end()) {
        return false;
    }
    
    // Create new index for this table
    tables_[name] = std::make_unique<TimeSeriesIndex>();
    table_types_[name] = type;
    
    return true;
}

bool TimeSeriesDB::dropTable(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return false;
    }
    
    tables_.erase(it);
    table_types_.erase(name);
    
    return true;
}

bool TimeSeriesDB::hasTable(const std::string& name) const {
    return tables_.find(name) != tables_.end();
}

std::vector<std::string> TimeSeriesDB::listTables() const {
    std::vector<std::string> names;
    names.reserve(tables_.size());
    
    for (const auto& [name, _] : tables_) {
        names.push_back(name);
    }
    
    return names;
}

size_t TimeSeriesDB::insert(const std::string& table_name, const TimeSeriesData& data) {
    auto* table_index = getTableIndex(table_name);
    if (!table_index) {
        throw std::runtime_error("Table not found: " + table_name);
    }
    
    ++write_count_;
    return table_index->add(data);
}

std::vector<size_t> TimeSeriesDB::insertBatch(const std::string& table_name,
                                                const std::vector<TimeSeriesData>& data_list) {
    auto* table_index = getTableIndex(table_name);
    if (!table_index) {
        throw std::runtime_error("Table not found: " + table_name);
    }
    
    write_count_ += data_list.size();
    return table_index->add_batch(data_list);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(const std::string& table_name,
                                                  const QueryConfig& config) const {
    const auto* table_index = getTableIndex(table_name);
    if (!table_index) {
        throw std::runtime_error("Table not found: " + table_name);
    }
    
    ++query_count_;
    return table_index->query(config);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(const std::string& table_name,
                                                  const TimeRange& time_range,
                                                  const Tags& filter_tags) const {
    QueryConfig config(time_range, filter_tags);
    config.limit = 0;  // No limit when querying by TimeRange
    return query(table_name, config);
}

TimeSeriesIndex* TimeSeriesDB::getTableIndex(const std::string& table_name) {
    auto it = tables_.find(table_name);
    if (it != tables_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const TimeSeriesIndex* TimeSeriesDB::getTableIndex(const std::string& table_name) const {
    auto it = tables_.find(table_name);
    if (it != tables_.end()) {
        return it->second.get();
    }
    return nullptr;
}

// ========== Resource Management Implementation ==========

std::shared_ptr<ResourceManager> TimeSeriesDB::getResourceManager() const {
    return resource_manager_;
}

void TimeSeriesDB::setResourceManager(std::shared_ptr<ResourceManager> resource_manager) {
    resource_manager_ = resource_manager;
}

// ========== Default Table API (backward compatible) ==========

size_t TimeSeriesDB::add(const TimeSeriesData& data) {
    ++write_count_;
    return index_->add(data);
}

size_t TimeSeriesDB::add(int64_t timestamp, double value,
                         const Tags& tags, const Fields& fields) {
    TimeSeriesData data(timestamp, value, tags);
    data.fields = fields;
    return add(data);
}

size_t TimeSeriesDB::add(int64_t timestamp, const std::vector<double>& value,
                         const Tags& tags, const Fields& fields) {
    TimeSeriesData data(timestamp, value, tags);
    data.fields = fields;
    return add(data);
}

std::vector<size_t> TimeSeriesDB::add_batch(
    const std::vector<TimeSeriesData>& data_list) {
    write_count_ += data_list.size();
    return index_->add_batch(data_list);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(
    const QueryConfig& config) const {
    ++query_count_;
    return index_->query(config);
}

std::vector<TimeSeriesData> TimeSeriesDB::query(
    const TimeRange& time_range, const Tags& filter_tags) const {
    QueryConfig config(time_range, filter_tags);
    return query(config);
}

void TimeSeriesDB::register_algorithm(
    const std::string& name,
    std::shared_ptr<TimeSeriesAlgorithm> algorithm) {
    algorithms_[name] = algorithm;
}

std::vector<TimeSeriesData> TimeSeriesDB::apply_algorithm(
    const std::string& name,
    const std::vector<TimeSeriesData>& data) const {
    auto it = algorithms_.find(name);
    if (it != algorithms_.end()) {
        return it->second->process(data);
    }
    return {};
}

bool TimeSeriesDB::has_algorithm(const std::string& name) const {
    return algorithms_.find(name) != algorithms_.end();
}

std::vector<std::string> TimeSeriesDB::list_algorithms() const {
    std::vector<std::string> names;
    for (const auto& [name, _] : algorithms_) {
        names.push_back(name);
    }
    return names;
}

size_t TimeSeriesDB::size() const {
    return index_->size();
}

bool TimeSeriesDB::empty() const {
    return index_->empty();
}

void TimeSeriesDB::clear() {
    index_->clear();
    query_count_ = 0;
    write_count_ = 0;
}

std::map<std::string, int64_t> TimeSeriesDB::get_stats() const {
    return {
        {"size", static_cast<int64_t>(size())},
        {"query_count", query_count_},
        {"write_count", write_count_},
        {"algorithm_count", static_cast<int64_t>(algorithms_.size())}
    };
}

// ========== Persistence Methods ==========

bool TimeSeriesDB::save_to_disk(const std::string& file_path) {
    if (!storage_engine_) {
        return false;
    }
    
    // Get all data from index
    TimeRange full_range(0, std::numeric_limits<int64_t>::max());
    QueryConfig config(full_range);
    std::vector<TimeSeriesData> all_data = index_->query(config);
    
    return storage_engine_->save(all_data, file_path);
}

bool TimeSeriesDB::load_from_disk(const std::string& file_path, bool clear_existing) {
    if (!storage_engine_) {
        return false;
    }
    
    std::vector<TimeSeriesData> loaded_data = storage_engine_->load(file_path);
    if (loaded_data.empty()) {
        return false;
    }
    
    if (clear_existing) {
        clear();
    }
    
    add_batch(loaded_data);
    return true;
}

bool TimeSeriesDB::create_checkpoint(uint64_t checkpoint_id) {
    if (!storage_engine_) {
        return false;
    }
    
    // Get all data from index
    TimeRange full_range(0, std::numeric_limits<int64_t>::max());
    QueryConfig config(full_range);
    std::vector<TimeSeriesData> all_data = index_->query(config);
    
    return storage_engine_->create_checkpoint(all_data, checkpoint_id);
}

bool TimeSeriesDB::restore_from_checkpoint(uint64_t checkpoint_id, bool clear_existing) {
    if (!storage_engine_) {
        return false;
    }
    
    std::vector<TimeSeriesData> checkpoint_data = storage_engine_->restore_checkpoint(checkpoint_id);
    if (checkpoint_data.empty()) {
        return false;
    }
    
    if (clear_existing) {
        clear();
    }
    
    add_batch(checkpoint_data);
    return true;
}

std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> 
TimeSeriesDB::list_checkpoints() const {
    std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> result;
    
    if (!storage_engine_) {
        return result;
    }
    
    std::vector<CheckpointInfo> checkpoints = storage_engine_->list_checkpoints();
    for (const auto& info : checkpoints) {
        std::map<std::string, int64_t> metadata;
        metadata["checkpoint_id"] = static_cast<int64_t>(info.checkpoint_id);
        metadata["timestamp"] = info.timestamp;
        metadata["data_count"] = static_cast<int64_t>(info.data_count);
        
        result.emplace_back(info.checkpoint_id, metadata);
    }
    
    return result;
}

bool TimeSeriesDB::delete_checkpoint(uint64_t checkpoint_id) {
    if (!storage_engine_) {
        return false;
    }
    
    return storage_engine_->delete_checkpoint(checkpoint_id);
}

void TimeSeriesDB::set_storage_path(const std::string& path) {
    if (storage_engine_) {
        storage_engine_->set_base_path(path);
    }
}

std::string TimeSeriesDB::get_storage_path() const {
    if (storage_engine_) {
        return storage_engine_->get_base_path();
    }
    return "";
}

void TimeSeriesDB::set_compression_enabled(bool enable) {
    if (storage_engine_) {
        storage_engine_->set_compression_enabled(enable);
    }
}

std::map<std::string, uint64_t> TimeSeriesDB::get_storage_stats() const {
    if (storage_engine_) {
        return storage_engine_->get_statistics();
    }
    return {};
}

} // namespace sage_tsdb
