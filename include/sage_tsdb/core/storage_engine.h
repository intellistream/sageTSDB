#pragma once

#include "time_series_data.h"
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace sage_tsdb {

/**
 * @brief Storage format version for compatibility checking
 */
constexpr uint32_t STORAGE_FORMAT_VERSION = 1;

/**
 * @brief Magic number to identify sageTSDB files
 */
constexpr uint32_t STORAGE_MAGIC_NUMBER = 0x53544442; // "STDB" in hex

/**
 * @brief File header structure
 */
struct FileHeader {
    uint32_t magic_number;        // Magic number for file identification
    uint32_t format_version;      // Storage format version
    uint64_t data_count;          // Number of data points
    uint64_t checkpoint_id;       // Checkpoint identifier
    int64_t min_timestamp;        // Minimum timestamp in file
    int64_t max_timestamp;        // Maximum timestamp in file
    uint64_t index_offset;        // Offset to index section
    uint64_t metadata_offset;     // Offset to metadata section
    
    FileHeader()
        : magic_number(STORAGE_MAGIC_NUMBER),
          format_version(STORAGE_FORMAT_VERSION),
          data_count(0),
          checkpoint_id(0),
          min_timestamp(0),
          max_timestamp(0),
          index_offset(0),
          metadata_offset(0) {}
};

/**
 * @brief Checkpoint metadata
 */
struct CheckpointInfo {
    uint64_t checkpoint_id;
    int64_t timestamp;            // When checkpoint was created
    uint64_t data_count;          // Number of data points at checkpoint
    std::string file_path;        // Path to checkpoint file
    
    CheckpointInfo()
        : checkpoint_id(0), timestamp(0), data_count(0) {}
    
    CheckpointInfo(uint64_t id, int64_t ts, uint64_t count, const std::string& path)
        : checkpoint_id(id), timestamp(ts), data_count(count), file_path(path) {}
};

/**
 * @brief Storage engine for persisting time series data
 * 
 * Features:
 * - Binary serialization for efficient storage
 * - Checkpoint support for incremental backups
 * - Automatic compression (future)
 * - WAL (Write-Ahead Logging) support (future)
 */
class StorageEngine {
public:
    StorageEngine();
    explicit StorageEngine(const std::string& base_path);
    ~StorageEngine() = default;
    
    /**
     * @brief Save time series data to disk
     * @param data Vector of time series data
     * @param file_path Path to save file
     * @return true if successful
     */
    bool save(const std::vector<TimeSeriesData>& data, const std::string& file_path);
    
    /**
     * @brief Load time series data from disk
     * @param file_path Path to load file
     * @return Vector of loaded data
     */
    std::vector<TimeSeriesData> load(const std::string& file_path);
    
    /**
     * @brief Create a checkpoint
     * @param data Vector of time series data
     * @param checkpoint_id Checkpoint identifier
     * @return true if successful
     */
    bool create_checkpoint(const std::vector<TimeSeriesData>& data, uint64_t checkpoint_id);
    
    /**
     * @brief Restore from a checkpoint
     * @param checkpoint_id Checkpoint identifier to restore
     * @return Vector of restored data
     */
    std::vector<TimeSeriesData> restore_checkpoint(uint64_t checkpoint_id);
    
    /**
     * @brief List all available checkpoints
     * @return Vector of checkpoint information
     */
    std::vector<CheckpointInfo> list_checkpoints() const;
    
    /**
     * @brief Delete a checkpoint
     * @param checkpoint_id Checkpoint identifier to delete
     * @return true if successful
     */
    bool delete_checkpoint(uint64_t checkpoint_id);
    
    /**
     * @brief Append data to existing file (incremental write)
     * @param data Vector of time series data to append
     * @param file_path Path to file
     * @return true if successful
     */
    bool append(const std::vector<TimeSeriesData>& data, const std::string& file_path);
    
    /**
     * @brief Get storage statistics
     * @return Map of statistics
     */
    std::map<std::string, uint64_t> get_statistics() const;
    
    /**
     * @brief Set base path for storage files
     * @param path Base directory path
     */
    void set_base_path(const std::string& path);
    
    /**
     * @brief Get base path
     * @return Base directory path
     */
    std::string get_base_path() const { return base_path_; }
    
    /**
     * @brief Enable/disable compression (future feature)
     * @param enable true to enable compression
     */
    void set_compression_enabled(bool enable) { compression_enabled_ = enable; }
    
    /**
     * @brief Check if compression is enabled
     * @return true if compression is enabled
     */
    bool is_compression_enabled() const { return compression_enabled_; }

private:
    /**
     * @brief Write file header
     */
    bool write_header(std::ofstream& out, const FileHeader& header);
    
    /**
     * @brief Read file header
     */
    bool read_header(std::ifstream& in, FileHeader& header);
    
    /**
     * @brief Serialize a single data point
     */
    bool write_data_point(std::ofstream& out, const TimeSeriesData& data);
    
    /**
     * @brief Deserialize a single data point
     */
    bool read_data_point(std::ifstream& in, TimeSeriesData& data);
    
    /**
     * @brief Write tags to stream
     */
    bool write_tags(std::ofstream& out, const Tags& tags);
    
    /**
     * @brief Read tags from stream
     */
    bool read_tags(std::ifstream& in, Tags& tags);
    
    /**
     * @brief Write fields to stream
     */
    bool write_fields(std::ofstream& out, const Fields& fields);
    
    /**
     * @brief Read fields from stream
     */
    bool read_fields(std::ifstream& in, Fields& fields);
    
    /**
     * @brief Get checkpoint file path
     */
    std::string get_checkpoint_path(uint64_t checkpoint_id) const;
    
    /**
     * @brief Get checkpoint metadata file path
     */
    std::string get_checkpoint_metadata_path() const;
    
    /**
     * @brief Load checkpoint metadata
     */
    void load_checkpoint_metadata();
    
    /**
     * @brief Save checkpoint metadata
     */
    bool save_checkpoint_metadata();
    
    /**
     * @brief Validate file header
     */
    bool validate_header(const FileHeader& header);
    
    std::string base_path_;                           // Base directory for storage
    std::map<uint64_t, CheckpointInfo> checkpoints_;  // Checkpoint registry
    bool compression_enabled_;                         // Compression flag
    uint64_t bytes_written_;                          // Total bytes written
    uint64_t bytes_read_;                             // Total bytes read
};

} // namespace sage_tsdb
