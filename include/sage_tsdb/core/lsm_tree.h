#pragma once

#include "time_series_data.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace sage_tsdb {

/**
 * @brief Bloom filter for fast existence checking
 */
class BloomFilter {
public:
    BloomFilter(size_t size, size_t num_hash_functions);
    ~BloomFilter() = default;
    
    void add(int64_t key);
    bool might_contain(int64_t key) const;
    void clear();
    
    // Serialization
    void serialize(std::ofstream& out) const;
    bool deserialize(std::ifstream& in);
    
private:
    std::vector<bool> bits_;
    size_t num_hash_functions_;
    
    size_t hash(int64_t key, size_t seed) const;
};

/**
 * @brief Write-Ahead Log for crash recovery
 */
class WriteAheadLog {
public:
    explicit WriteAheadLog(const std::string& log_path);
    ~WriteAheadLog();
    
    bool append(int64_t timestamp, const TimeSeriesData& data);
    std::map<int64_t, TimeSeriesData> recover();
    bool clear();
    bool sync();
    
private:
    std::string log_path_;
    std::ofstream log_file_;
    std::mutex mutex_;
};

/**
 * @brief In-memory sorted table (skip list based)
 */
class MemTable {
public:
    MemTable(size_t max_size_bytes = 4 * 1024 * 1024); // 4MB default
    ~MemTable() = default;
    
    bool put(int64_t timestamp, const TimeSeriesData& data);
    bool get(int64_t timestamp, TimeSeriesData& data) const;
    bool is_full() const;
    size_t size() const { return data_.size(); }
    size_t size_bytes() const { return size_bytes_; }
    
    // Range query
    std::vector<TimeSeriesData> range_query(int64_t start_time, int64_t end_time) const;
    
    // Get all data (for flushing to SSTable)
    const std::map<int64_t, TimeSeriesData>& get_all() const { return data_; }
    
    void clear();
    
private:
    std::map<int64_t, TimeSeriesData> data_;
    size_t max_size_bytes_;
    std::atomic<size_t> size_bytes_;
    mutable std::mutex mutex_;
    
    size_t estimate_size(const TimeSeriesData& data) const;
};

/**
 * @brief Sorted String Table (immutable on-disk file)
 */
class SSTable {
public:
    struct Metadata {
        uint32_t magic_number;        // 0x53535442 "SSTB"
        uint32_t version;
        uint64_t level;               // LSM tree level
        uint64_t sequence_number;     // For ordering SSTables
        uint64_t num_entries;
        int64_t min_timestamp;
        int64_t max_timestamp;
        uint64_t bloom_filter_offset;
        uint64_t index_offset;
        uint64_t data_offset;
        
        Metadata();
    };
    
    struct IndexEntry {
        int64_t timestamp;
        uint64_t offset;            // Offset in data section
        uint32_t size;              // Size of data block
    };
    
    SSTable(const std::string& file_path, uint64_t level, uint64_t sequence);
    ~SSTable() = default;
    
    // Build from MemTable
    bool build_from_memtable(const std::map<int64_t, TimeSeriesData>& data);
    
    // Build from multiple SSTables (for compaction)
    bool build_from_sstables(const std::vector<std::shared_ptr<SSTable>>& sstables);
    
    // Query operations
    bool get(int64_t timestamp, TimeSeriesData& data);
    std::vector<TimeSeriesData> range_query(int64_t start_time, int64_t end_time);
    
    // Metadata accessors
    uint64_t get_level() const { return metadata_.level; }
    uint64_t get_sequence() const { return metadata_.sequence_number; }
    int64_t get_min_timestamp() const { return metadata_.min_timestamp; }
    int64_t get_max_timestamp() const { return metadata_.max_timestamp; }
    size_t get_num_entries() const { return metadata_.num_entries; }
    std::string get_file_path() const { return file_path_; }
    
    // Check if timestamp might be in this SSTable
    bool might_contain(int64_t timestamp);
    
    // Get file size
    size_t get_file_size() const;
    
private:
    std::string file_path_;
    Metadata metadata_;
    std::unique_ptr<BloomFilter> bloom_filter_;
    std::vector<IndexEntry> index_;
    mutable std::mutex mutex_;
    
    bool write_metadata(std::ofstream& out);
    bool read_metadata(std::ifstream& in);
    bool write_bloom_filter(std::ofstream& out);
    bool read_bloom_filter(std::ifstream& in);
    bool write_index(std::ofstream& out);
    bool read_index(std::ifstream& in);
    bool write_data(std::ofstream& out, const std::map<int64_t, TimeSeriesData>& data);
    bool read_data_at(std::ifstream& in, uint64_t offset, uint32_t size, TimeSeriesData& data);
};

/**
 * @brief LSM Tree configuration
 */
struct LSMConfig {
    size_t memtable_size_bytes = 4 * 1024 * 1024;      // 4MB
    size_t level0_file_num_compaction_trigger = 4;     // Trigger compaction when 4 files
    size_t max_levels = 7;                              // Maximum number of levels
    size_t level_size_multiplier = 10;                  // Each level is 10x larger
    size_t bloom_filter_bits_per_key = 10;              // Bloom filter size
    bool enable_compression = false;                     // Future: compression
    std::string data_dir = "./lsm_data";                // Data directory
    
    LSMConfig() = default;
};

/**
 * @brief LSM Tree for efficient time series storage
 * 
 * Features:
 * - Write-optimized with MemTable and WAL
 * - Leveled compaction strategy
 * - Bloom filters for fast negative lookups
 * - Range query support
 * - Crash recovery via WAL
 */
class LSMTree {
public:
    explicit LSMTree(const LSMConfig& config = LSMConfig());
    ~LSMTree();
    
    // Basic operations
    bool put(int64_t timestamp, const TimeSeriesData& data);
    bool get(int64_t timestamp, TimeSeriesData& data);
    std::vector<TimeSeriesData> range_query(int64_t start_time, int64_t end_time);
    
    // Batch operations
    bool put_batch(const std::vector<TimeSeriesData>& data_batch);
    
    // Flush operations
    bool flush();  // Flush current MemTable to disk
    
    // Compaction
    void trigger_compaction();
    void wait_for_compaction();
    
    // Statistics
    struct Statistics {
        uint64_t total_puts = 0;
        uint64_t total_gets = 0;
        uint64_t memtable_hits = 0;
        uint64_t sstable_hits = 0;
        uint64_t bloom_filter_rejections = 0;
        uint64_t compactions = 0;
        size_t num_sstables = 0;
        size_t total_size_bytes = 0;
    };
    
    Statistics get_statistics() const;
    
    // Maintenance
    void clear_all();
    bool recover_from_wal();
    
    // Configuration
    LSMConfig get_config() const { return config_; }
    
private:
    LSMConfig config_;
    
    // MemTable
    std::unique_ptr<MemTable> active_memtable_;
    std::unique_ptr<MemTable> immutable_memtable_;
    
    // WAL
    std::unique_ptr<WriteAheadLog> wal_;
    
    // SSTables organized by level
    std::map<uint64_t, std::vector<std::shared_ptr<SSTable>>> levels_;
    
    // Synchronization
    mutable std::mutex memtable_mutex_;
    mutable std::mutex sstable_mutex_;
    std::mutex compaction_mutex_;
    std::condition_variable compaction_cv_;
    
    // Background compaction thread
    std::thread compaction_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> compaction_needed_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Statistics stats_;
    
    // Sequence number for SSTables
    std::atomic<uint64_t> next_sequence_;
    
    // Private methods
    void compaction_worker();
    void flush_memtable_to_l0();
    void compact_level(uint64_t level);
    std::vector<std::shared_ptr<SSTable>> select_sstables_for_compaction(uint64_t level);
    bool merge_sstables(const std::vector<std::shared_ptr<SSTable>>& sstables, 
                       uint64_t target_level);
    
    std::string generate_sstable_path(uint64_t level, uint64_t sequence);
    bool load_existing_sstables();
    
    // Query helpers
    bool search_in_memtables(int64_t timestamp, TimeSeriesData& data);
    bool search_in_sstables(int64_t timestamp, TimeSeriesData& data);
    
    void merge_range_results(std::vector<TimeSeriesData>& results,
                            const std::vector<TimeSeriesData>& new_results);
};

} // namespace sage_tsdb
