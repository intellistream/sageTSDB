#include "sage_tsdb/core/backends/memory_backend.h"

#include <stdexcept>

namespace sage_tsdb {
namespace core {

MemoryBackend::MemoryBackend(const StorageBackendConfig& /*config*/) {
    // MemoryBackend has no configurable parameters; config is accepted only so
    // it can be constructed uniformly through StorageBackendRegistry::create().
}

// ---- Table management ---------------------------------------------------

bool MemoryBackend::createTable(const std::string& name, TableType type) {
    std::lock_guard<std::mutex> lock(tables_mutex_);
    if (tables_.find(name) != tables_.end()) {
        return false;  // already exists
    }
    tables_[name] = std::make_unique<TimeSeriesIndex>();
    table_types_[name] = type;
    return true;
}

bool MemoryBackend::dropTable(const std::string& name) {
    std::lock_guard<std::mutex> lock(tables_mutex_);
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        return false;
    }
    tables_.erase(it);
    table_types_.erase(name);
    return true;
}

bool MemoryBackend::hasTable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(tables_mutex_);
    return tables_.find(name) != tables_.end();
}

std::vector<std::string> MemoryBackend::listTables() const {
    std::lock_guard<std::mutex> lock(tables_mutex_);
    std::vector<std::string> names;
    names.reserve(tables_.size());
    for (const auto& kv : tables_) {
        names.push_back(kv.first);
    }
    return names;
}

// ---- Writes -------------------------------------------------------------

size_t MemoryBackend::insert(const std::string& table,
                             const TimeSeriesData& data) {
    TimeSeriesIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(tables_mutex_);
        index = findIndex(table);
    }
    if (!index) {
        throw std::runtime_error("Table not found: " + table);
    }
    return index->add(data);
}

std::vector<size_t> MemoryBackend::insertBatch(
    const std::string& table,
    const std::vector<TimeSeriesData>& data_list) {
    TimeSeriesIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(tables_mutex_);
        index = findIndex(table);
    }
    if (!index) {
        throw std::runtime_error("Table not found: " + table);
    }
    return index->add_batch(data_list);
}

// ---- Queries ------------------------------------------------------------

std::vector<TimeSeriesData> MemoryBackend::query(
    const std::string& table, const QueryConfig& config) const {
    const TimeSeriesIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(tables_mutex_);
        index = findIndex(table);
    }
    if (!index) {
        throw std::runtime_error("Table not found: " + table);
    }
    return index->query(config);
}

// ---- Statistics / lifecycle --------------------------------------------

size_t MemoryBackend::size(const std::string& table) const {
    const TimeSeriesIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(tables_mutex_);
        index = findIndex(table);
    }
    return index ? index->size() : 0;
}

void MemoryBackend::clear(const std::string& table) {
    TimeSeriesIndex* index = nullptr;
    {
        std::lock_guard<std::mutex> lock(tables_mutex_);
        index = findIndex(table);
    }
    if (index) {
        index->clear();
    }
}

bool MemoryBackend::flush() {
    // In-memory data is always "durable" within the process lifetime.
    return true;
}

// ---- Helpers ------------------------------------------------------------

TimeSeriesIndex* MemoryBackend::findIndex(const std::string& name) {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

const TimeSeriesIndex* MemoryBackend::findIndex(const std::string& name) const {
    auto it = tables_.find(name);
    return it == tables_.end() ? nullptr : it->second.get();
}

} // namespace core
} // namespace sage_tsdb

// Register MemoryBackend under the name "memory" (default backend).
REGISTER_STORAGE_BACKEND("memory", ::sage_tsdb::core::MemoryBackend)
