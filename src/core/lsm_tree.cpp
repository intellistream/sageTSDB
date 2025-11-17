#include "sage_tsdb/core/lsm_tree.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace sage_tsdb {

namespace fs = std::filesystem;

// ============================================================================
// BloomFilter Implementation
// ============================================================================

BloomFilter::BloomFilter(size_t size, size_t num_hash_functions)
    : bits_(size, false), num_hash_functions_(num_hash_functions) {}

void BloomFilter::add(int64_t key) {
    for (size_t i = 0; i < num_hash_functions_; ++i) {
        size_t idx = hash(key, i) % bits_.size();
        bits_[idx] = true;
    }
}

bool BloomFilter::might_contain(int64_t key) const {
    for (size_t i = 0; i < num_hash_functions_; ++i) {
        size_t idx = hash(key, i) % bits_.size();
        if (!bits_[idx]) {
            return false;
        }
    }
    return true;
}

void BloomFilter::clear() {
    std::fill(bits_.begin(), bits_.end(), false);
}

size_t BloomFilter::hash(int64_t key, size_t seed) const {
    // Simple hash function combining key and seed
    size_t h = seed;
    h ^= static_cast<size_t>(key) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= static_cast<size_t>(key >> 32) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

void BloomFilter::serialize(std::ofstream& out) const {
    // Write size
    uint64_t size = bits_.size();
    out.write(reinterpret_cast<const char*>(&size), sizeof(size));
    
    // Write num hash functions
    uint64_t num_hash = num_hash_functions_;
    out.write(reinterpret_cast<const char*>(&num_hash), sizeof(num_hash));
    
    // Write bits in chunks of 8 (as bytes)
    std::vector<uint8_t> bytes((bits_.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits_.size(); ++i) {
        if (bits_[i]) {
            bytes[i / 8] |= (1 << (i % 8));
        }
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool BloomFilter::deserialize(std::ifstream& in) {
    // Read size
    uint64_t size;
    in.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (!in) return false;
    
    // Read num hash functions
    uint64_t num_hash;
    in.read(reinterpret_cast<char*>(&num_hash), sizeof(num_hash));
    if (!in) return false;
    
    num_hash_functions_ = num_hash;
    bits_.resize(size, false);
    
    // Read bits
    std::vector<uint8_t> bytes((size + 7) / 8);
    in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (!in) return false;
    
    for (size_t i = 0; i < size; ++i) {
        bits_[i] = (bytes[i / 8] & (1 << (i % 8))) != 0;
    }
    
    return true;
}

// ============================================================================
// WriteAheadLog Implementation
// ============================================================================

WriteAheadLog::WriteAheadLog(const std::string& log_path)
    : log_path_(log_path) {
    // Open log file in append mode
    log_file_.open(log_path_, std::ios::binary | std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Failed to open WAL: " << log_path_ << std::endl;
    }
}

WriteAheadLog::~WriteAheadLog() {
    if (log_file_.is_open()) {
        log_file_.flush();
        log_file_.close();
    }
}

bool WriteAheadLog::append(int64_t timestamp, const TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!log_file_.is_open()) {
        return false;
    }
    
    // Write timestamp
    log_file_.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
    
    // Write value type (0 = scalar, 1 = vector)
    uint8_t value_type = data.is_scalar() ? 0 : 1;
    log_file_.write(reinterpret_cast<const char*>(&value_type), sizeof(value_type));
    
    // Write value
    if (data.is_scalar()) {
        double val = data.as_double();
        log_file_.write(reinterpret_cast<const char*>(&val), sizeof(val));
    } else {
        auto vec = data.as_vector();
        uint64_t vec_size = vec.size();
        log_file_.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
        log_file_.write(reinterpret_cast<const char*>(vec.data()), vec_size * sizeof(double));
    }
    
    // Write tags
    uint32_t num_tags = data.tags.size();
    log_file_.write(reinterpret_cast<const char*>(&num_tags), sizeof(num_tags));
    for (const auto& [key, value] : data.tags) {
        uint32_t key_len = key.size();
        uint32_t val_len = value.size();
        log_file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        log_file_.write(key.data(), key_len);
        log_file_.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        log_file_.write(value.data(), val_len);
    }
    
    // Write fields
    uint32_t num_fields = data.fields.size();
    log_file_.write(reinterpret_cast<const char*>(&num_fields), sizeof(num_fields));
    for (const auto& [key, value] : data.fields) {
        uint32_t key_len = key.size();
        uint32_t val_len = value.size();
        log_file_.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        log_file_.write(key.data(), key_len);
        log_file_.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
        log_file_.write(value.data(), val_len);
    }
    
    return log_file_.good();
}

std::map<int64_t, TimeSeriesData> WriteAheadLog::recover() {
    std::map<int64_t, TimeSeriesData> result;
    
    std::ifstream in(log_path_, std::ios::binary);
    if (!in.is_open()) {
        return result;
    }
    
    while (in.peek() != EOF) {
        // Read timestamp
        int64_t timestamp;
        in.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
        if (!in) break;
        
        TimeSeriesData data;
        data.timestamp = timestamp;
        
        // Read value type
        uint8_t value_type;
        in.read(reinterpret_cast<char*>(&value_type), sizeof(value_type));
        if (!in) break;
        
        // Read value
        if (value_type == 0) {
            double val;
            in.read(reinterpret_cast<char*>(&val), sizeof(val));
            if (!in) break;
            data.value = val;
        } else {
            uint64_t vec_size;
            in.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
            if (!in) break;
            
            std::vector<double> vec(vec_size);
            in.read(reinterpret_cast<char*>(vec.data()), vec_size * sizeof(double));
            if (!in) break;
            data.value = vec;
        }
        
        // Read tags
        uint32_t num_tags;
        in.read(reinterpret_cast<char*>(&num_tags), sizeof(num_tags));
        if (!in) break;
        
        for (uint32_t i = 0; i < num_tags; ++i) {
            uint32_t key_len, val_len;
            in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (!in) break;
            
            std::string key(key_len, '\0');
            in.read(&key[0], key_len);
            if (!in) break;
            
            in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (!in) break;
            
            std::string value(val_len, '\0');
            in.read(&value[0], val_len);
            if (!in) break;
            
            data.tags[key] = value;
        }
        
        // Read fields
        uint32_t num_fields;
        in.read(reinterpret_cast<char*>(&num_fields), sizeof(num_fields));
        if (!in) break;
        
        for (uint32_t i = 0; i < num_fields; ++i) {
            uint32_t key_len, val_len;
            in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (!in) break;
            
            std::string key(key_len, '\0');
            in.read(&key[0], key_len);
            if (!in) break;
            
            in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (!in) break;
            
            std::string value(val_len, '\0');
            in.read(&value[0], val_len);
            if (!in) break;
            
            data.fields[key] = value;
        }
        
        result[timestamp] = data;
    }
    
    in.close();
    return result;
}

bool WriteAheadLog::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
    
    // Remove old file
    if (fs::exists(log_path_)) {
        fs::remove(log_path_);
    }
    
    // Reopen fresh file
    log_file_.open(log_path_, std::ios::binary | std::ios::app);
    return log_file_.is_open();
}

bool WriteAheadLog::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (log_file_.is_open()) {
        log_file_.flush();
        return log_file_.good();
    }
    return false;
}

// ============================================================================
// MemTable Implementation
// ============================================================================

MemTable::MemTable(size_t max_size_bytes)
    : max_size_bytes_(max_size_bytes), size_bytes_(0) {}

bool MemTable::put(int64_t timestamp, const TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t data_size = estimate_size(data);
    
    // Check if inserting would exceed max size
    if (size_bytes_ + data_size > max_size_bytes_ && data_.find(timestamp) == data_.end()) {
        return false; // MemTable is full
    }
    
    // If updating existing entry, adjust size
    auto it = data_.find(timestamp);
    if (it != data_.end()) {
        size_bytes_ -= estimate_size(it->second);
    }
    
    data_[timestamp] = data;
    size_bytes_ += data_size;
    
    return true;
}

bool MemTable::get(int64_t timestamp, TimeSeriesData& data) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(timestamp);
    if (it != data_.end()) {
        data = it->second;
        return true;
    }
    return false;
}

bool MemTable::is_full() const {
    return size_bytes_ >= max_size_bytes_;
}

std::vector<TimeSeriesData> MemTable::range_query(int64_t start_time, int64_t end_time) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TimeSeriesData> result;
    
    auto it_start = data_.lower_bound(start_time);
    auto it_end = data_.upper_bound(end_time);
    
    for (auto it = it_start; it != it_end; ++it) {
        result.push_back(it->second);
    }
    
    return result;
}

void MemTable::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    size_bytes_ = 0;
}

size_t MemTable::estimate_size(const TimeSeriesData& data) const {
    size_t size = sizeof(int64_t); // timestamp
    
    // Value size
    if (data.is_scalar()) {
        size += sizeof(double);
    } else {
        auto vec = data.as_vector();
        size += sizeof(size_t) + vec.size() * sizeof(double);
    }
    
    // Tags size
    for (const auto& [key, value] : data.tags) {
        size += key.size() + value.size() + 2 * sizeof(size_t);
    }
    
    // Fields size
    for (const auto& [key, value] : data.fields) {
        size += key.size() + value.size() + 2 * sizeof(size_t);
    }
    
    return size;
}

// ============================================================================
// SSTable Implementation
// ============================================================================

SSTable::Metadata::Metadata()
    : magic_number(0x53535442), // "SSTB"
      version(1),
      level(0),
      sequence_number(0),
      num_entries(0),
      min_timestamp(0),
      max_timestamp(0),
      bloom_filter_offset(0),
      index_offset(0),
      data_offset(0) {}

SSTable::SSTable(const std::string& file_path, uint64_t level, uint64_t sequence)
    : file_path_(file_path) {
    metadata_.level = level;
    metadata_.sequence_number = sequence;
}

bool SSTable::build_from_memtable(const std::map<int64_t, TimeSeriesData>& data) {
    if (data.empty()) {
        return false;
    }
    
    std::ofstream out(file_path_, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to create SSTable: " << file_path_ << std::endl;
        return false;
    }
    
    // Prepare metadata
    metadata_.num_entries = data.size();
    metadata_.min_timestamp = data.begin()->first;
    metadata_.max_timestamp = data.rbegin()->first;
    
    // Reserve space for metadata (will write later)
    size_t metadata_size = sizeof(SSTable::Metadata);
    std::vector<char> metadata_buffer(metadata_size, 0);
    out.write(metadata_buffer.data(), metadata_size);
    
    // Create bloom filter
    size_t bloom_bits = data.size() * 10; // 10 bits per key
    bloom_filter_ = std::make_unique<BloomFilter>(bloom_bits, 3);
    for (const auto& [ts, _] : data) {
        bloom_filter_->add(ts);
    }
    
    // Write bloom filter
    metadata_.bloom_filter_offset = out.tellp();
    bloom_filter_->serialize(out);
    
    // Write index
    metadata_.index_offset = out.tellp();
    index_.clear();
    index_.reserve(data.size());
    
    // Reserve space for index
    size_t index_size = data.size() * (sizeof(int64_t) + sizeof(uint64_t) + sizeof(uint32_t));
    std::vector<char> index_buffer(index_size, 0);
    out.write(index_buffer.data(), index_size);
    
    // Write data and build index
    metadata_.data_offset = out.tellp();
    for (const auto& [timestamp, ts_data] : data) {
        IndexEntry entry;
        entry.timestamp = timestamp;
        entry.offset = out.tellp();
        
        // Write timestamp
        out.write(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
        
        // Write value type and value
        uint8_t value_type = ts_data.is_scalar() ? 0 : 1;
        out.write(reinterpret_cast<const char*>(&value_type), sizeof(value_type));
        
        if (ts_data.is_scalar()) {
            double val = ts_data.as_double();
            out.write(reinterpret_cast<const char*>(&val), sizeof(val));
        } else {
            auto vec = ts_data.as_vector();
            uint64_t vec_size = vec.size();
            out.write(reinterpret_cast<const char*>(&vec_size), sizeof(vec_size));
            out.write(reinterpret_cast<const char*>(vec.data()), vec_size * sizeof(double));
        }
        
        // Write tags
        uint32_t num_tags = ts_data.tags.size();
        out.write(reinterpret_cast<const char*>(&num_tags), sizeof(num_tags));
        for (const auto& [key, value] : ts_data.tags) {
            uint32_t key_len = key.size();
            uint32_t val_len = value.size();
            out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            out.write(key.data(), key_len);
            out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            out.write(value.data(), val_len);
        }
        
        // Write fields
        uint32_t num_fields = ts_data.fields.size();
        out.write(reinterpret_cast<const char*>(&num_fields), sizeof(num_fields));
        for (const auto& [key, value] : ts_data.fields) {
            uint32_t key_len = key.size();
            uint32_t val_len = value.size();
            out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            out.write(key.data(), key_len);
            out.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            out.write(value.data(), val_len);
        }
        
        entry.size = static_cast<uint32_t>(static_cast<uint64_t>(out.tellp()) - entry.offset);
        index_.push_back(entry);
    }
    
    // Write index back to reserved space
    out.seekp(metadata_.index_offset);
    for (const auto& entry : index_) {
        out.write(reinterpret_cast<const char*>(&entry.timestamp), sizeof(entry.timestamp));
        out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        out.write(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));
    }
    
    // Write metadata to beginning
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(&metadata_), sizeof(metadata_));
    
    out.close();
    return true;
}

bool SSTable::get(int64_t timestamp, TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check bloom filter first
    if (bloom_filter_ && !bloom_filter_->might_contain(timestamp)) {
        return false;
    }
    
    // Load metadata and index if not already loaded
    if (index_.empty()) {
        std::ifstream in(file_path_, std::ios::binary);
        if (!in.is_open()) return false;
        
        if (!read_metadata(in)) return false;
        if (!read_bloom_filter(in)) return false;
        if (!read_index(in)) return false;
        
        in.close();
    }
    
    // Binary search in index
    auto it = std::lower_bound(index_.begin(), index_.end(), timestamp,
        [](const IndexEntry& entry, int64_t ts) {
            return entry.timestamp < ts;
        });
    
    if (it == index_.end() || it->timestamp != timestamp) {
        return false;
    }
    
    // Read data at offset
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) return false;
    
    return read_data_at(in, it->offset, it->size, data);
}

std::vector<TimeSeriesData> SSTable::range_query(int64_t start_time, int64_t end_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<TimeSeriesData> result;
    
    // Load index if not already loaded
    if (index_.empty()) {
        std::ifstream in(file_path_, std::ios::binary);
        if (!in.is_open()) return result;
        
        if (!read_metadata(in)) return result;
        if (!read_bloom_filter(in)) return result;
        if (!read_index(in)) return result;
        
        in.close();
    }
    
    // Find range in index
    auto it_start = std::lower_bound(index_.begin(), index_.end(), start_time,
        [](const IndexEntry& entry, int64_t ts) {
            return entry.timestamp < ts;
        });
    
    auto it_end = std::upper_bound(index_.begin(), index_.end(), end_time,
        [](int64_t ts, const IndexEntry& entry) {
            return ts < entry.timestamp;
        });
    
    // Read all entries in range
    std::ifstream in(file_path_, std::ios::binary);
    if (!in.is_open()) return result;
    
    for (auto it = it_start; it != it_end; ++it) {
        TimeSeriesData data;
        if (read_data_at(in, it->offset, it->size, data)) {
            result.push_back(data);
        }
    }
    
    in.close();
    return result;
}

bool SSTable::might_contain(int64_t timestamp) {
    if (timestamp < metadata_.min_timestamp || timestamp > metadata_.max_timestamp) {
        return false;
    }
    
    if (bloom_filter_) {
        return bloom_filter_->might_contain(timestamp);
    }
    
    return true;
}

size_t SSTable::get_file_size() const {
    if (fs::exists(file_path_)) {
        return fs::file_size(file_path_);
    }
    return 0;
}

bool SSTable::write_metadata(std::ofstream& out) {
    out.write(reinterpret_cast<const char*>(&metadata_), sizeof(metadata_));
    return out.good();
}

bool SSTable::read_metadata(std::ifstream& in) {
    in.seekg(0);
    in.read(reinterpret_cast<char*>(&metadata_), sizeof(metadata_));
    return in.good() && metadata_.magic_number == 0x53535442;
}

bool SSTable::read_bloom_filter(std::ifstream& in) {
    in.seekg(metadata_.bloom_filter_offset);
    
    if (!bloom_filter_) {
        bloom_filter_ = std::make_unique<BloomFilter>(100, 3);
    }
    
    return bloom_filter_->deserialize(in);
}

bool SSTable::read_index(std::ifstream& in) {
    in.seekg(metadata_.index_offset);
    
    index_.clear();
    index_.reserve(metadata_.num_entries);
    
    for (uint64_t i = 0; i < metadata_.num_entries; ++i) {
        IndexEntry entry;
        in.read(reinterpret_cast<char*>(&entry.timestamp), sizeof(entry.timestamp));
        in.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset));
        in.read(reinterpret_cast<char*>(&entry.size), sizeof(entry.size));
        
        if (!in) return false;
        index_.push_back(entry);
    }
    
    return true;
}

bool SSTable::read_data_at(std::ifstream& in, uint64_t offset, uint32_t size, TimeSeriesData& data) {
    in.seekg(offset);
    
    // Read timestamp
    int64_t timestamp;
    in.read(reinterpret_cast<char*>(&timestamp), sizeof(timestamp));
    if (!in) return false;
    data.timestamp = timestamp;
    
    // Read value type
    uint8_t value_type;
    in.read(reinterpret_cast<char*>(&value_type), sizeof(value_type));
    if (!in) return false;
    
    // Read value
    if (value_type == 0) {
        double val;
        in.read(reinterpret_cast<char*>(&val), sizeof(val));
        if (!in) return false;
        data.value = val;
    } else {
        uint64_t vec_size;
        in.read(reinterpret_cast<char*>(&vec_size), sizeof(vec_size));
        if (!in) return false;
        
        std::vector<double> vec(vec_size);
        in.read(reinterpret_cast<char*>(vec.data()), vec_size * sizeof(double));
        if (!in) return false;
        data.value = vec;
    }
    
    // Read tags
    uint32_t num_tags;
    in.read(reinterpret_cast<char*>(&num_tags), sizeof(num_tags));
    if (!in) return false;
    
    for (uint32_t i = 0; i < num_tags; ++i) {
        uint32_t key_len, val_len;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (!in) return false;
        
        std::string key(key_len, '\0');
        in.read(&key[0], key_len);
        if (!in) return false;
        
        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        if (!in) return false;
        
        std::string value(val_len, '\0');
        in.read(&value[0], val_len);
        if (!in) return false;
        
        data.tags[key] = value;
    }
    
    // Read fields
    uint32_t num_fields;
    in.read(reinterpret_cast<char*>(&num_fields), sizeof(num_fields));
    if (!in) return false;
    
    for (uint32_t i = 0; i < num_fields; ++i) {
        uint32_t key_len, val_len;
        in.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        if (!in) return false;
        
        std::string key(key_len, '\0');
        in.read(&key[0], key_len);
        if (!in) return false;
        
        in.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        if (!in) return false;
        
        std::string value(val_len, '\0');
        in.read(&value[0], val_len);
        if (!in) return false;
        
        data.fields[key] = value;
    }
    
    return true;
}

bool SSTable::build_from_sstables(const std::vector<std::shared_ptr<SSTable>>& sstables) {
    // Merge multiple SSTables into one
    std::map<int64_t, TimeSeriesData> merged_data;
    
    for (const auto& sstable : sstables) {
        auto range_data = sstable->range_query(INT64_MIN, INT64_MAX);
        for (const auto& data : range_data) {
            merged_data[data.timestamp] = data;
        }
    }
    
    return build_from_memtable(merged_data);
}

// ============================================================================
// LSMTree Implementation
// ============================================================================

LSMTree::LSMTree(const LSMConfig& config)
    : config_(config),
      running_(true),
      compaction_needed_(false),
      next_sequence_(0) {
    
    // Create data directory
    if (!fs::exists(config_.data_dir)) {
        fs::create_directories(config_.data_dir);
    }
    
    // Initialize MemTable
    active_memtable_ = std::make_unique<MemTable>(config_.memtable_size_bytes);
    
    // Initialize WAL
    std::string wal_path = config_.data_dir + "/wal.log";
    wal_ = std::make_unique<WriteAheadLog>(wal_path);
    
    // Load existing SSTables
    load_existing_sstables();
    
    // Start compaction thread
    compaction_thread_ = std::thread(&LSMTree::compaction_worker, this);
}

LSMTree::~LSMTree() {
    running_ = false;
    compaction_cv_.notify_all();
    
    if (compaction_thread_.joinable()) {
        compaction_thread_.join();
    }
    
    // Flush any remaining data
    flush();
}

bool LSMTree::put(int64_t timestamp, const TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(memtable_mutex_);
    
    // Write to WAL first
    if (!wal_->append(timestamp, data)) {
        std::cerr << "Failed to write to WAL" << std::endl;
        return false;
    }
    
    // Try to insert into active MemTable
    if (!active_memtable_->put(timestamp, data)) {
        // MemTable is full, need to flush
        immutable_memtable_ = std::move(active_memtable_);
        active_memtable_ = std::make_unique<MemTable>(config_.memtable_size_bytes);
        
        // Flush immutable MemTable in background
        flush_memtable_to_l0();
        
        // Try again with new MemTable
        if (!active_memtable_->put(timestamp, data)) {
            std::cerr << "Failed to insert into new MemTable" << std::endl;
            return false;
        }
    }
    
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_puts++;
    }
    
    return true;
}

bool LSMTree::put_batch(const std::vector<TimeSeriesData>& data_batch) {
    for (const auto& data : data_batch) {
        if (!put(data.timestamp, data)) {
            return false;
        }
    }
    return true;
}

bool LSMTree::get(int64_t timestamp, TimeSeriesData& data) {
    // Update statistics
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_gets++;
    }
    
    // Search in MemTables first
    if (search_in_memtables(timestamp, data)) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.memtable_hits++;
        return true;
    }
    
    // Search in SSTables
    if (search_in_sstables(timestamp, data)) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.sstable_hits++;
        return true;
    }
    
    return false;
}

std::vector<TimeSeriesData> LSMTree::range_query(int64_t start_time, int64_t end_time) {
    std::vector<TimeSeriesData> result;
    
    // Query MemTables
    {
        std::lock_guard<std::mutex> lock(memtable_mutex_);
        
        auto active_results = active_memtable_->range_query(start_time, end_time);
        merge_range_results(result, active_results);
        
        if (immutable_memtable_) {
            auto immutable_results = immutable_memtable_->range_query(start_time, end_time);
            merge_range_results(result, immutable_results);
        }
    }
    
    // Query SSTables
    {
        std::lock_guard<std::mutex> lock(sstable_mutex_);
        
        for (const auto& [level, sstables] : levels_) {
            for (const auto& sstable : sstables) {
                if (sstable->get_min_timestamp() <= end_time && 
                    sstable->get_max_timestamp() >= start_time) {
                    auto sstable_results = sstable->range_query(start_time, end_time);
                    merge_range_results(result, sstable_results);
                }
            }
        }
    }
    
    return result;
}

bool LSMTree::flush() {
    std::lock_guard<std::mutex> lock(memtable_mutex_);
    
    if (active_memtable_->size() > 0) {
        immutable_memtable_ = std::move(active_memtable_);
        active_memtable_ = std::make_unique<MemTable>(config_.memtable_size_bytes);
        
        flush_memtable_to_l0();
    }
    
    return true;
}

void LSMTree::trigger_compaction() {
    compaction_needed_ = true;
    compaction_cv_.notify_one();
}

void LSMTree::wait_for_compaction() {
    std::unique_lock<std::mutex> lock(compaction_mutex_);
    compaction_cv_.wait(lock, [this] { return !compaction_needed_; });
}

LSMTree::Statistics LSMTree::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    Statistics stats = stats_;
    
    // Update SSTable info
    std::lock_guard<std::mutex> sstable_lock(sstable_mutex_);
    for (const auto& [level, sstables] : levels_) {
        stats.num_sstables += sstables.size();
        for (const auto& sstable : sstables) {
            stats.total_size_bytes += sstable->get_file_size();
        }
    }
    
    return stats;
}

void LSMTree::clear_all() {
    std::lock_guard<std::mutex> memtable_lock(memtable_mutex_);
    std::lock_guard<std::mutex> sstable_lock(sstable_mutex_);
    
    // Clear MemTables
    active_memtable_->clear();
    immutable_memtable_.reset();
    
    // Clear WAL
    wal_->clear();
    
    // Delete all SSTables
    for (auto& [level, sstables] : levels_) {
        for (auto& sstable : sstables) {
            std::string file_path = sstable->get_file_path();
            if (fs::exists(file_path)) {
                fs::remove(file_path);
            }
        }
    }
    levels_.clear();
    
    // Reset statistics
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_ = Statistics();
}

bool LSMTree::recover_from_wal() {
    auto recovered_data = wal_->recover();
    
    if (recovered_data.empty()) {
        return true;
    }
    
    std::cout << "Recovering " << recovered_data.size() << " entries from WAL" << std::endl;
    
    for (const auto& [timestamp, data] : recovered_data) {
        active_memtable_->put(timestamp, data);
    }
    
    // Clear WAL after recovery
    wal_->clear();
    
    return true;
}

// Private methods

void LSMTree::compaction_worker() {
    while (running_) {
        std::unique_lock<std::mutex> lock(compaction_mutex_);
        compaction_cv_.wait_for(lock, std::chrono::seconds(1), 
            [this] { return compaction_needed_.load() || !running_.load(); });
        
        if (!running_) break;
        
        if (compaction_needed_) {
            // Check level 0
            {
                std::lock_guard<std::mutex> sstable_lock(sstable_mutex_);
                if (levels_[0].size() >= config_.level0_file_num_compaction_trigger) {
                    compact_level(0);
                }
            }
            
            // Check other levels
            for (uint64_t level = 1; level < config_.max_levels - 1; ++level) {
                std::lock_guard<std::mutex> sstable_lock(sstable_mutex_);
                size_t level_size = 0;
                for (const auto& sstable : levels_[level]) {
                    level_size += sstable->get_file_size();
                }
                
                size_t max_size = (1ULL << level) * config_.level_size_multiplier * 1024 * 1024;
                if (level_size > max_size) {
                    compact_level(level);
                }
            }
            
            compaction_needed_ = false;
            
            // Update statistics
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.compactions++;
        }
    }
}

void LSMTree::flush_memtable_to_l0() {
    if (!immutable_memtable_ || immutable_memtable_->size() == 0) {
        return;
    }
    
    // Create new SSTable
    uint64_t sequence = next_sequence_++;
    std::string sstable_path = generate_sstable_path(0, sequence);
    
    auto sstable = std::make_shared<SSTable>(sstable_path, 0, sequence);
    
    if (sstable->build_from_memtable(immutable_memtable_->get_all())) {
        std::lock_guard<std::mutex> lock(sstable_mutex_);
        levels_[0].push_back(sstable);
        
        // Clear WAL after successful flush
        wal_->clear();
        
        // Reset immutable MemTable
        immutable_memtable_.reset();
        
        // Trigger compaction if needed
        if (levels_[0].size() >= config_.level0_file_num_compaction_trigger) {
            trigger_compaction();
        }
    } else {
        std::cerr << "Failed to flush MemTable to SSTable" << std::endl;
    }
}

void LSMTree::compact_level(uint64_t level) {
    auto sstables = select_sstables_for_compaction(level);
    
    if (sstables.empty()) {
        return;
    }
    
    merge_sstables(sstables, level + 1);
    
    // Remove old SSTables
    for (const auto& sstable : sstables) {
        auto& level_sstables = levels_[level];
        level_sstables.erase(
            std::remove(level_sstables.begin(), level_sstables.end(), sstable),
            level_sstables.end()
        );
        
        // Delete file
        std::string file_path = sstable->get_file_path();
        if (fs::exists(file_path)) {
            fs::remove(file_path);
        }
    }
}

std::vector<std::shared_ptr<SSTable>> LSMTree::select_sstables_for_compaction(uint64_t level) {
    std::vector<std::shared_ptr<SSTable>> selected;
    
    if (levels_.find(level) == levels_.end() || levels_[level].empty()) {
        return selected;
    }
    
    // For level 0, select all files (they may overlap)
    if (level == 0) {
        selected = levels_[0];
    } else {
        // For other levels, select oldest file
        auto& sstables = levels_[level];
        if (!sstables.empty()) {
            selected.push_back(sstables[0]);
        }
    }
    
    return selected;
}

bool LSMTree::merge_sstables(const std::vector<std::shared_ptr<SSTable>>& sstables, 
                             uint64_t target_level) {
    if (sstables.empty()) {
        return false;
    }
    
    // Create new SSTable
    uint64_t sequence = next_sequence_++;
    std::string sstable_path = generate_sstable_path(target_level, sequence);
    
    auto new_sstable = std::make_shared<SSTable>(sstable_path, target_level, sequence);
    
    if (new_sstable->build_from_sstables(sstables)) {
        levels_[target_level].push_back(new_sstable);
        return true;
    }
    
    return false;
}

std::string LSMTree::generate_sstable_path(uint64_t level, uint64_t sequence) {
    std::ostringstream oss;
    oss << config_.data_dir << "/L" << level << "_" << sequence << ".sst";
    return oss.str();
}

bool LSMTree::load_existing_sstables() {
    if (!fs::exists(config_.data_dir)) {
        return true;
    }
    
    // Scan directory for SSTable files
    for (const auto& entry : fs::directory_iterator(config_.data_dir)) {
        if (entry.path().extension() == ".sst") {
            std::string filename = entry.path().filename().string();
            
            // Parse level and sequence from filename (format: L<level>_<sequence>.sst)
            size_t underscore_pos = filename.find('_');
            if (underscore_pos == std::string::npos) continue;
            
            uint64_t level = std::stoull(filename.substr(1, underscore_pos - 1));
            uint64_t sequence = std::stoull(filename.substr(underscore_pos + 1));
            
            auto sstable = std::make_shared<SSTable>(entry.path().string(), level, sequence);
            levels_[level].push_back(sstable);
            
            if (sequence >= next_sequence_) {
                next_sequence_ = sequence + 1;
            }
        }
    }
    
    return true;
}

bool LSMTree::search_in_memtables(int64_t timestamp, TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(memtable_mutex_);
    
    // Search active MemTable first
    if (active_memtable_->get(timestamp, data)) {
        return true;
    }
    
    // Search immutable MemTable
    if (immutable_memtable_ && immutable_memtable_->get(timestamp, data)) {
        return true;
    }
    
    return false;
}

bool LSMTree::search_in_sstables(int64_t timestamp, TimeSeriesData& data) {
    std::lock_guard<std::mutex> lock(sstable_mutex_);
    
    // Search from level 0 to highest level
    for (uint64_t level = 0; level < config_.max_levels; ++level) {
        if (levels_.find(level) == levels_.end()) {
            continue;
        }
        
        for (auto& sstable : levels_[level]) {
            // Use bloom filter for quick rejection
            if (!sstable->might_contain(timestamp)) {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                stats_.bloom_filter_rejections++;
                continue;
            }
            
            if (sstable->get(timestamp, data)) {
                return true;
            }
        }
    }
    
    return false;
}

void LSMTree::merge_range_results(std::vector<TimeSeriesData>& results,
                                  const std::vector<TimeSeriesData>& new_results) {
    // Merge and deduplicate based on timestamp
    std::map<int64_t, TimeSeriesData> merged;
    
    for (const auto& data : results) {
        merged[data.timestamp] = data;
    }
    
    for (const auto& data : new_results) {
        merged[data.timestamp] = data;
    }
    
    results.clear();
    for (const auto& [_, data] : merged) {
        results.push_back(data);
    }
}

} // namespace sage_tsdb
