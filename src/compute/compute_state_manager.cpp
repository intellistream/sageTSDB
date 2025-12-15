#include "sage_tsdb/compute/compute_state_manager.h"
#include "sage_tsdb/core/time_series_db.h"
#include <cstring>
#include <stdexcept>
#include <chrono>

namespace sage_tsdb {
namespace compute {

namespace {
    // Helper: Get current timestamp in milliseconds
    int64_t getCurrentTimestamp() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }
}

ComputeStateManager::ComputeStateManager(TimeSeriesDB* db)
    : db_(db) {
    if (!db_) {
        throw std::invalid_argument("TimeSeriesDB pointer cannot be null");
    }
    ensureTablesExist();
}

ComputeStateManager::~ComputeStateManager() = default;

void ComputeStateManager::ensureTablesExist() {
    // Create state table if not exists
    if (!db_->hasTable(STATE_TABLE_NAME)) {
        db_->createTable(STATE_TABLE_NAME, TableType::ComputeState);
    }
    
    // Create checkpoint table if not exists
    if (!db_->hasTable(CHECKPOINT_TABLE_NAME)) {
        db_->createTable(CHECKPOINT_TABLE_NAME, TableType::ComputeState);
    }
}

bool ComputeStateManager::saveState(const std::string& compute_name, 
                                     const ComputeState& state) {
    if (compute_name.empty()) {
        return false;
    }
    
    try {
        // Convert state to TimeSeriesData
        TimeSeriesData data = stateToData(state);
        
        // Delete old state (if exists)
        // Note: In a real implementation, we'd use an update operation
        // For now, we rely on the latest timestamp
        
        // Insert new state
        db_->insert(STATE_TABLE_NAME, data);
        
        return true;
    } catch (const std::exception& e) {
        // Log error (in production, use proper logging)
        return false;
    }
}

bool ComputeStateManager::loadState(const std::string& compute_name, 
                                     ComputeState& state) {
    if (compute_name.empty()) {
        return false;
    }
    
    try {
        // Query latest state for this compute engine
        Tags filter_tags = {{"compute_name", compute_name}};
        
        // Query all states for this compute engine
        auto results = db_->query(STATE_TABLE_NAME, 
                                 TimeRange(0, std::numeric_limits<int64_t>::max()),
                                 filter_tags);
        
        if (results.empty()) {
            return false;
        }
        
        // Get the latest state (highest timestamp)
        const TimeSeriesData* latest = &results[0];
        for (const auto& result : results) {
            if (result.timestamp > latest->timestamp) {
                latest = &result;
            }
        }
        
        // Convert to ComputeState
        return dataToState(*latest, state);
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool ComputeStateManager::hasState(const std::string& compute_name) const {
    if (compute_name.empty()) {
        return false;
    }
    
    try {
        Tags filter_tags = {{"compute_name", compute_name}};
        auto results = db_->query(STATE_TABLE_NAME,
                                 TimeRange(0, std::numeric_limits<int64_t>::max()),
                                 filter_tags);
        return !results.empty();
    } catch (const std::exception& e) {
        return false;
    }
}

bool ComputeStateManager::deleteState(const std::string& compute_name) {
    // Note: In a real LSM-Tree implementation, we'd insert a tombstone record
    // For now, this is a placeholder
    // The actual deletion would happen during compaction
    return true;
}

std::vector<std::string> ComputeStateManager::listStates() const {
    std::vector<std::string> compute_names;
    
    try {
        // Query all states
        auto results = db_->query(STATE_TABLE_NAME,
                                 TimeRange(0, std::numeric_limits<int64_t>::max()));
        
        // Extract unique compute names
        std::set<std::string> unique_names;
        for (const auto& result : results) {
            auto it = result.tags.find("compute_name");
            if (it != result.tags.end()) {
                unique_names.insert(it->second);
            }
        }
        
        compute_names.assign(unique_names.begin(), unique_names.end());
        
    } catch (const std::exception& e) {
        // Return empty vector on error
    }
    
    return compute_names;
}

bool ComputeStateManager::persistState(const std::string& compute_name) {
    // In a real implementation, this would trigger:
    // 1. Flush MemTable to immutable MemTable
    // 2. Write immutable MemTable to SSTable (Level 0)
    // 3. Sync to disk
    
    // For now, use the database's checkpoint mechanism
    try {
        uint64_t checkpoint_id = static_cast<uint64_t>(getCurrentTimestamp());
        return db_->create_checkpoint(checkpoint_id);
    } catch (const std::exception& e) {
        return false;
    }
}

bool ComputeStateManager::createCheckpoint(const std::string& compute_name, 
                                           uint64_t checkpoint_id) {
    if (compute_name.empty()) {
        return false;
    }
    
    try {
        // Load current state
        ComputeState current_state;
        if (!loadState(compute_name, current_state)) {
            return false;
        }
        
        // Create checkpoint record
        TimeSeriesData checkpoint_data;
        checkpoint_data.timestamp = getCurrentTimestamp();
        checkpoint_data.tags = {
            {"compute_name", compute_name},
            {"checkpoint_id", std::to_string(checkpoint_id)}
        };
        checkpoint_data.fields = {
            {"watermark", std::to_string(current_state.watermark)},
            {"window_id", std::to_string(current_state.window_id)},
            {"processed_events", std::to_string(current_state.processed_events)}
        };
        
        // Serialize operator state
        auto serialized = serialize(current_state);
        checkpoint_data.value = std::vector<double>(serialized.begin(), serialized.end());
        
        // Insert checkpoint
        db_->insert(CHECKPOINT_TABLE_NAME, checkpoint_data);
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

bool ComputeStateManager::restoreCheckpoint(const std::string& compute_name,
                                            uint64_t checkpoint_id,
                                            ComputeState& state) {
    if (compute_name.empty()) {
        return false;
    }
    
    try {
        // Query checkpoint
        Tags filter_tags = {
            {"compute_name", compute_name},
            {"checkpoint_id", std::to_string(checkpoint_id)}
        };
        
        auto results = db_->query(CHECKPOINT_TABLE_NAME,
                                 TimeRange(0, std::numeric_limits<int64_t>::max()),
                                 filter_tags);
        
        if (results.empty()) {
            return false;
        }
        
        // Deserialize from latest matching record
        const auto& checkpoint_data = results[0];
        
        // Extract fields
        state.compute_name = compute_name;
        state.timestamp = checkpoint_data.timestamp;
        
        auto watermark_it = checkpoint_data.fields.find("watermark");
        if (watermark_it != checkpoint_data.fields.end()) {
            state.watermark = std::stoll(watermark_it->second);
        }
        
        auto window_id_it = checkpoint_data.fields.find("window_id");
        if (window_id_it != checkpoint_data.fields.end()) {
            state.window_id = std::stoull(window_id_it->second);
        }
        
        auto processed_events_it = checkpoint_data.fields.find("processed_events");
        if (processed_events_it != checkpoint_data.fields.end()) {
            state.processed_events = std::stoull(processed_events_it->second);
        }
        
        // Deserialize operator state
        if (std::holds_alternative<std::vector<double>>(checkpoint_data.value)) {
            const auto& vec = std::get<std::vector<double>>(checkpoint_data.value);
            state.operator_state.resize(vec.size());
            std::transform(vec.begin(), vec.end(), state.operator_state.begin(),
                          [](double d) { return static_cast<uint8_t>(d); });
        }
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>>
ComputeStateManager::listCheckpoints(const std::string& compute_name) const {
    std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> result;
    
    if (compute_name.empty()) {
        return result;
    }
    
    try {
        Tags filter_tags = {{"compute_name", compute_name}};
        auto results = db_->query(CHECKPOINT_TABLE_NAME,
                                 TimeRange(0, std::numeric_limits<int64_t>::max()),
                                 filter_tags);
        
        for (const auto& data : results) {
            auto checkpoint_id_it = data.tags.find("checkpoint_id");
            if (checkpoint_id_it == data.tags.end()) {
                continue;
            }
            
            uint64_t checkpoint_id = std::stoull(checkpoint_id_it->second);
            
            std::map<std::string, int64_t> metadata;
            metadata["timestamp"] = data.timestamp;
            
            if (auto it = data.fields.find("window_id"); it != data.fields.end()) {
                metadata["window_id"] = std::stoll(it->second);
            }
            if (auto it = data.fields.find("processed_events"); it != data.fields.end()) {
                metadata["processed_events"] = std::stoll(it->second);
            }
            
            result.emplace_back(checkpoint_id, metadata);
        }
        
    } catch (const std::exception& e) {
        // Return empty vector on error
    }
    
    return result;
}

bool ComputeStateManager::deleteCheckpoint(const std::string& compute_name,
                                           uint64_t checkpoint_id) {
    // Similar to deleteState, this would insert a tombstone in a real implementation
    return true;
}

// ========== Serialization Implementation ==========

std::vector<uint8_t> ComputeStateManager::serialize(const ComputeState& state) {
    // Simple binary serialization format:
    // [compute_name_len:4][compute_name:N][timestamp:8][watermark:8]
    // [window_id:8][processed_events:8][operator_state_len:4][operator_state:N]
    // [metadata_count:4][metadata_entries...]
    
    std::vector<uint8_t> buffer;
    buffer.reserve(1024); // Initial allocation
    
    // Helper lambda to append data
    auto append = [&buffer](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buffer.insert(buffer.end(), bytes, bytes + size);
    };
    
    // Serialize compute_name
    uint32_t name_len = static_cast<uint32_t>(state.compute_name.size());
    append(&name_len, sizeof(name_len));
    append(state.compute_name.data(), state.compute_name.size());
    
    // Serialize numeric fields
    append(&state.timestamp, sizeof(state.timestamp));
    append(&state.watermark, sizeof(state.watermark));
    append(&state.window_id, sizeof(state.window_id));
    append(&state.processed_events, sizeof(state.processed_events));
    
    // Serialize operator_state
    uint32_t operator_state_len = static_cast<uint32_t>(state.operator_state.size());
    append(&operator_state_len, sizeof(operator_state_len));
    if (operator_state_len > 0) {
        append(state.operator_state.data(), state.operator_state.size());
    }
    
    // Serialize metadata
    uint32_t metadata_count = static_cast<uint32_t>(state.metadata.size());
    append(&metadata_count, sizeof(metadata_count));
    for (const auto& [key, value] : state.metadata) {
        uint32_t key_len = static_cast<uint32_t>(key.size());
        uint32_t value_len = static_cast<uint32_t>(value.size());
        append(&key_len, sizeof(key_len));
        append(key.data(), key.size());
        append(&value_len, sizeof(value_len));
        append(value.data(), value.size());
    }
    
    return buffer;
}

bool ComputeStateManager::deserialize(const std::vector<uint8_t>& data, 
                                      ComputeState& state) {
    if (data.size() < 32) { // Minimum size check
        return false;
    }
    
    size_t offset = 0;
    
    // Helper lambda to read data
    auto read = [&data, &offset](void* dest, size_t size) -> bool {
        if (offset + size > data.size()) {
            return false;
        }
        std::memcpy(dest, data.data() + offset, size);
        offset += size;
        return true;
    };
    
    // Helper lambda to read string
    auto read_string = [&read]() -> std::string {
        uint32_t len;
        if (!read(&len, sizeof(len))) {
            return "";
        }
        std::string str(len, '\0');
        if (!read(&str[0], len)) {
            return "";
        }
        return str;
    };
    
    try {
        // Deserialize compute_name
        state.compute_name = read_string();
        
        // Deserialize numeric fields
        if (!read(&state.timestamp, sizeof(state.timestamp))) return false;
        if (!read(&state.watermark, sizeof(state.watermark))) return false;
        if (!read(&state.window_id, sizeof(state.window_id))) return false;
        if (!read(&state.processed_events, sizeof(state.processed_events))) return false;
        
        // Deserialize operator_state
        uint32_t operator_state_len;
        if (!read(&operator_state_len, sizeof(operator_state_len))) return false;
        state.operator_state.resize(operator_state_len);
        if (operator_state_len > 0) {
            if (!read(state.operator_state.data(), operator_state_len)) return false;
        }
        
        // Deserialize metadata
        uint32_t metadata_count;
        if (!read(&metadata_count, sizeof(metadata_count))) return false;
        state.metadata.clear();
        for (uint32_t i = 0; i < metadata_count; ++i) {
            std::string key = read_string();
            std::string value = read_string();
            state.metadata[key] = value;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

// ========== Private Helper Methods ==========

TimeSeriesData ComputeStateManager::stateToData(const ComputeState& state) const {
    TimeSeriesData data;
    data.timestamp = getCurrentTimestamp();
    
    // Store compute_name as tag
    data.tags = {{"compute_name", state.compute_name}};
    
    // Store fields
    data.fields = {
        {"watermark", std::to_string(state.watermark)},
        {"window_id", std::to_string(state.window_id)},
        {"processed_events", std::to_string(state.processed_events)}
    };
    
    // Add metadata
    for (const auto& [key, value] : state.metadata) {
        data.fields["meta_" + key] = value;
    }
    
    // Serialize operator state to value
    auto serialized = serialize(state);
    data.value = std::vector<double>(serialized.begin(), serialized.end());
    
    return data;
}

bool ComputeStateManager::dataToState(const TimeSeriesData& data, 
                                      ComputeState& state) const {
    // Extract compute_name from tags
    auto compute_name_it = data.tags.find("compute_name");
    if (compute_name_it == data.tags.end()) {
        return false;
    }
    state.compute_name = compute_name_it->second;
    state.timestamp = data.timestamp;
    
    // Extract fields
    if (auto it = data.fields.find("watermark"); it != data.fields.end()) {
        state.watermark = std::stoll(it->second);
    }
    if (auto it = data.fields.find("window_id"); it != data.fields.end()) {
        state.window_id = std::stoull(it->second);
    }
    if (auto it = data.fields.find("processed_events"); it != data.fields.end()) {
        state.processed_events = std::stoull(it->second);
    }
    
    // Extract metadata (fields prefixed with "meta_")
    state.metadata.clear();
    for (const auto& [key, value] : data.fields) {
        if (key.find("meta_") == 0) {
            state.metadata[key.substr(5)] = value;
        }
    }
    
    // Deserialize operator state from value
    if (std::holds_alternative<std::vector<double>>(data.value)) {
        const auto& vec = std::get<std::vector<double>>(data.value);
        state.operator_state.resize(vec.size());
        std::transform(vec.begin(), vec.end(), state.operator_state.begin(),
                      [](double d) { return static_cast<uint8_t>(d); });
    }
    
    return true;
}

} // namespace compute
} // namespace sage_tsdb
