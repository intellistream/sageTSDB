#include "sage_tsdb/core/storage_engine.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

namespace sage_tsdb {

namespace fs = std::filesystem;

StorageEngine::StorageEngine()
    : base_path_("./sage_tsdb_data"),
      compression_enabled_(false),
      bytes_written_(0),
      bytes_read_(0) {
    // Create base directory if it doesn't exist
    if (!fs::exists(base_path_)) {
        fs::create_directories(base_path_);
    }
    load_checkpoint_metadata();
}

StorageEngine::StorageEngine(const std::string& base_path)
    : base_path_(base_path),
      compression_enabled_(false),
      bytes_written_(0),
      bytes_read_(0) {
    // Create base directory if it doesn't exist
    if (!fs::exists(base_path_)) {
        fs::create_directories(base_path_);
    }
    load_checkpoint_metadata();
}

void StorageEngine::set_base_path(const std::string& path) {
    base_path_ = path;
    if (!fs::exists(base_path_)) {
        fs::create_directories(base_path_);
    }
    load_checkpoint_metadata();
}

bool StorageEngine::save(const std::vector<TimeSeriesData>& data, const std::string& file_path) {
    if (data.empty()) {
        return true; // Nothing to save
    }
    
    std::ofstream out(file_path, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open file for writing: " << file_path << std::endl;
        return false;
    }
    
    // Prepare header
    FileHeader header;
    header.data_count = data.size();
    header.min_timestamp = data.front().timestamp;
    header.max_timestamp = data.back().timestamp;
    
    // Find actual min/max timestamps
    for (const auto& point : data) {
        if (point.timestamp < header.min_timestamp) {
            header.min_timestamp = point.timestamp;
        }
        if (point.timestamp > header.max_timestamp) {
            header.max_timestamp = point.timestamp;
        }
    }
    
    // Write header
    if (!write_header(out, header)) {
        std::cerr << "Failed to write header" << std::endl;
        return false;
    }
    
    // Write data points
    for (const auto& point : data) {
        if (!write_data_point(out, point)) {
            std::cerr << "Failed to write data point" << std::endl;
            return false;
        }
    }
    
    bytes_written_ += out.tellp();
    out.close();
    return true;
}

std::vector<TimeSeriesData> StorageEngine::load(const std::string& file_path) {
    std::vector<TimeSeriesData> result;
    
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open file for reading: " << file_path << std::endl;
        return result;
    }
    
    // Read header
    FileHeader header;
    if (!read_header(in, header)) {
        std::cerr << "Failed to read header" << std::endl;
        return result;
    }
    
    // Validate header
    if (!validate_header(header)) {
        std::cerr << "Invalid file header" << std::endl;
        return result;
    }
    
    // Read data points
    result.reserve(header.data_count);
    for (uint64_t i = 0; i < header.data_count; ++i) {
        TimeSeriesData data;
        if (!read_data_point(in, data)) {
            std::cerr << "Failed to read data point " << i << std::endl;
            break;
        }
        result.push_back(std::move(data));
    }
    
    bytes_read_ += in.tellg();
    in.close();
    return result;
}

bool StorageEngine::create_checkpoint(const std::vector<TimeSeriesData>& data, 
                                       uint64_t checkpoint_id) {
    std::string checkpoint_path = get_checkpoint_path(checkpoint_id);
    
    if (!save(data, checkpoint_path)) {
        return false;
    }
    
    // Record checkpoint metadata
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    CheckpointInfo info(checkpoint_id, timestamp, data.size(), checkpoint_path);
    checkpoints_[checkpoint_id] = info;
    
    return save_checkpoint_metadata();
}

std::vector<TimeSeriesData> StorageEngine::restore_checkpoint(uint64_t checkpoint_id) {
    auto it = checkpoints_.find(checkpoint_id);
    if (it == checkpoints_.end()) {
        std::cerr << "Checkpoint " << checkpoint_id << " not found" << std::endl;
        return {};
    }
    
    return load(it->second.file_path);
}

std::vector<CheckpointInfo> StorageEngine::list_checkpoints() const {
    std::vector<CheckpointInfo> result;
    result.reserve(checkpoints_.size());
    
    for (const auto& [id, info] : checkpoints_) {
        result.push_back(info);
    }
    
    // Sort by checkpoint_id
    std::sort(result.begin(), result.end(), 
              [](const CheckpointInfo& a, const CheckpointInfo& b) {
                  return a.checkpoint_id < b.checkpoint_id;
              });
    
    return result;
}

bool StorageEngine::delete_checkpoint(uint64_t checkpoint_id) {
    auto it = checkpoints_.find(checkpoint_id);
    if (it == checkpoints_.end()) {
        return false;
    }
    
    // Delete checkpoint file
    std::string checkpoint_path = it->second.file_path;
    if (fs::exists(checkpoint_path)) {
        fs::remove(checkpoint_path);
    }
    
    // Remove from registry
    checkpoints_.erase(it);
    
    return save_checkpoint_metadata();
}

bool StorageEngine::append(const std::vector<TimeSeriesData>& data, 
                           const std::string& file_path) {
    if (data.empty()) {
        return true;
    }
    
    // Load existing data
    std::vector<TimeSeriesData> existing_data = load(file_path);
    
    // Append new data
    existing_data.insert(existing_data.end(), data.begin(), data.end());
    
    // Save combined data
    return save(existing_data, file_path);
}

std::map<std::string, uint64_t> StorageEngine::get_statistics() const {
    std::map<std::string, uint64_t> stats;
    stats["bytes_written"] = bytes_written_;
    stats["bytes_read"] = bytes_read_;
    stats["checkpoint_count"] = checkpoints_.size();
    stats["compression_enabled"] = compression_enabled_ ? 1 : 0;
    return stats;
}

// Private helper methods

bool StorageEngine::write_header(std::ofstream& out, const FileHeader& header) {
    out.write(reinterpret_cast<const char*>(&header.magic_number), sizeof(header.magic_number));
    out.write(reinterpret_cast<const char*>(&header.format_version), sizeof(header.format_version));
    out.write(reinterpret_cast<const char*>(&header.data_count), sizeof(header.data_count));
    out.write(reinterpret_cast<const char*>(&header.checkpoint_id), sizeof(header.checkpoint_id));
    out.write(reinterpret_cast<const char*>(&header.min_timestamp), sizeof(header.min_timestamp));
    out.write(reinterpret_cast<const char*>(&header.max_timestamp), sizeof(header.max_timestamp));
    out.write(reinterpret_cast<const char*>(&header.index_offset), sizeof(header.index_offset));
    out.write(reinterpret_cast<const char*>(&header.metadata_offset), sizeof(header.metadata_offset));
    return out.good();
}

bool StorageEngine::read_header(std::ifstream& in, FileHeader& header) {
    in.read(reinterpret_cast<char*>(&header.magic_number), sizeof(header.magic_number));
    in.read(reinterpret_cast<char*>(&header.format_version), sizeof(header.format_version));
    in.read(reinterpret_cast<char*>(&header.data_count), sizeof(header.data_count));
    in.read(reinterpret_cast<char*>(&header.checkpoint_id), sizeof(header.checkpoint_id));
    in.read(reinterpret_cast<char*>(&header.min_timestamp), sizeof(header.min_timestamp));
    in.read(reinterpret_cast<char*>(&header.max_timestamp), sizeof(header.max_timestamp));
    in.read(reinterpret_cast<char*>(&header.index_offset), sizeof(header.index_offset));
    in.read(reinterpret_cast<char*>(&header.metadata_offset), sizeof(header.metadata_offset));
    return in.good();
}

bool StorageEngine::write_data_point(std::ofstream& out, const TimeSeriesData& data) {
    // Write timestamp
    out.write(reinterpret_cast<const char*>(&data.timestamp), sizeof(data.timestamp));
    
    // Write value type and value
    bool is_scalar = data.is_scalar();
    out.write(reinterpret_cast<const char*>(&is_scalar), sizeof(is_scalar));
    
    if (is_scalar) {
        double val = data.as_double();
        out.write(reinterpret_cast<const char*>(&val), sizeof(val));
    } else {
        std::vector<double> vec = data.as_vector();
        uint64_t size = vec.size();
        out.write(reinterpret_cast<const char*>(&size), sizeof(size));
        out.write(reinterpret_cast<const char*>(vec.data()), size * sizeof(double));
    }
    
    // Write tags
    if (!write_tags(out, data.tags)) {
        return false;
    }
    
    // Write fields
    if (!write_fields(out, data.fields)) {
        return false;
    }
    
    return out.good();
}

bool StorageEngine::read_data_point(std::ifstream& in, TimeSeriesData& data) {
    // Read timestamp
    in.read(reinterpret_cast<char*>(&data.timestamp), sizeof(data.timestamp));
    
    // Read value type and value
    bool is_scalar;
    in.read(reinterpret_cast<char*>(&is_scalar), sizeof(is_scalar));
    
    if (is_scalar) {
        double val;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        data.value = val;
    } else {
        uint64_t size;
        in.read(reinterpret_cast<char*>(&size), sizeof(size));
        std::vector<double> vec(size);
        in.read(reinterpret_cast<char*>(vec.data()), size * sizeof(double));
        data.value = std::move(vec);
    }
    
    // Read tags
    if (!read_tags(in, data.tags)) {
        return false;
    }
    
    // Read fields
    if (!read_fields(in, data.fields)) {
        return false;
    }
    
    return in.good();
}

bool StorageEngine::write_tags(std::ofstream& out, const Tags& tags) {
    uint64_t count = tags.size();
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& [key, value] : tags) {
        // Write key
        uint64_t key_len = key.size();
        out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        out.write(key.data(), key_len);
        
        // Write value
        uint64_t val_len = value.size();
        out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        out.write(value.data(), val_len);
    }
    
    return out.good();
}

bool StorageEngine::read_tags(std::ifstream& in, Tags& tags) {
    uint64_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (uint64_t i = 0; i < count; ++i) {
        // Read key
        uint64_t key_len;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::string key(key_len, '\0');
        in.read(&key[0], key_len);
        
        // Read value
        uint64_t val_len;
        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        std::string value(val_len, '\0');
        in.read(&value[0], val_len);
        
        tags[key] = value;
    }
    
    return in.good();
}

bool StorageEngine::write_fields(std::ofstream& out, const Fields& fields) {
    // Fields use same format as tags
    return write_tags(out, fields);
}

bool StorageEngine::read_fields(std::ifstream& in, Fields& fields) {
    // Fields use same format as tags
    return read_tags(in, fields);
}

std::string StorageEngine::get_checkpoint_path(uint64_t checkpoint_id) const {
    std::ostringstream oss;
    oss << base_path_ << "/checkpoint_" << checkpoint_id << ".tsdb";
    return oss.str();
}

std::string StorageEngine::get_checkpoint_metadata_path() const {
    return base_path_ + "/checkpoints.meta";
}

void StorageEngine::load_checkpoint_metadata() {
    std::string meta_path = get_checkpoint_metadata_path();
    if (!fs::exists(meta_path)) {
        return;
    }
    
    std::ifstream in(meta_path, std::ios::binary);
    if (!in.is_open()) {
        return;
    }
    
    uint64_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (uint64_t i = 0; i < count; ++i) {
        CheckpointInfo info;
        in.read(reinterpret_cast<char*>(&info.checkpoint_id), sizeof(info.checkpoint_id));
        in.read(reinterpret_cast<char*>(&info.timestamp), sizeof(info.timestamp));
        in.read(reinterpret_cast<char*>(&info.data_count), sizeof(info.data_count));
        
        uint64_t path_len;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        info.file_path.resize(path_len);
        in.read(&info.file_path[0], path_len);
        
        checkpoints_[info.checkpoint_id] = info;
    }
    
    in.close();
}

bool StorageEngine::save_checkpoint_metadata() {
    std::string meta_path = get_checkpoint_metadata_path();
    std::ofstream out(meta_path, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    
    uint64_t count = checkpoints_.size();
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& [id, info] : checkpoints_) {
        out.write(reinterpret_cast<const char*>(&info.checkpoint_id), sizeof(info.checkpoint_id));
        out.write(reinterpret_cast<const char*>(&info.timestamp), sizeof(info.timestamp));
        out.write(reinterpret_cast<const char*>(&info.data_count), sizeof(info.data_count));
        
        uint64_t path_len = info.file_path.size();
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        out.write(info.file_path.data(), path_len);
    }
    
    out.close();
    return true;
}

bool StorageEngine::validate_header(const FileHeader& header) {
    if (header.magic_number != STORAGE_MAGIC_NUMBER) {
        std::cerr << "Invalid magic number: " << std::hex << header.magic_number << std::endl;
        return false;
    }
    
    if (header.format_version > STORAGE_FORMAT_VERSION) {
        std::cerr << "Unsupported format version: " << header.format_version << std::endl;
        return false;
    }
    
    return true;
}

} // namespace sage_tsdb
