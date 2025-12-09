#pragma once

#include "time_series_data.h"
#include "time_series_index.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace sage_tsdb {

// Forward declarations
class TimeSeriesAlgorithm;
class ResourceManager;

/**
 * @brief Table type enumeration
 * 
 * Defines different table types for specialized storage and query patterns
 */
enum class TableType {
    TimeSeries,      ///< General time series data (default)
    Stream,          ///< Stream input data (optimized for append)
    JoinResult,      ///< Join computation results
    ComputeState     ///< Compute engine internal state
};

/**
 * @brief Main time series database class
 * 
 * Provides high-level API for:
 * - Adding time series data
 * - Querying with time ranges and filters
 * - Registering and applying algorithms
 * - Database statistics
 */
class TimeSeriesDB {
public:
    TimeSeriesDB();
    ~TimeSeriesDB();
    
    // ========== Multi-Table Management API ==========
    
    /**
     * @brief Create a named table with specified type
     * @param name Table name (e.g., "stream_s", "join_results")
     * @param type Table type (default: TimeSeries)
     * @return true if created successfully, false if already exists
     * 
     * Example:
     *   db.createTable("stream_s", TableType::Stream);
     *   db.createTable("join_results", TableType::JoinResult);
     */
    bool createTable(const std::string& name, TableType type = TableType::TimeSeries);
    
    /**
     * @brief Drop a table
     * @param name Table name
     * @return true if dropped successfully
     */
    bool dropTable(const std::string& name);
    
    /**
     * @brief Check if table exists
     * @param name Table name
     * @return true if exists
     */
    bool hasTable(const std::string& name) const;
    
    /**
     * @brief List all table names
     * @return Vector of table names
     */
    std::vector<std::string> listTables() const;
    
    /**
     * @brief Insert data into a specific table
     * @param table_name Target table name
     * @param data Time series data
     * @return Index of added data
     * 
     * Example:
     *   db.insert("stream_s", data);
     */
    size_t insert(const std::string& table_name, const TimeSeriesData& data);
    
    /**
     * @brief Insert batch data into a specific table
     * @param table_name Target table name
     * @param data_list Vector of time series data
     * @return Vector of indices
     */
    std::vector<size_t> insertBatch(const std::string& table_name, 
                                     const std::vector<TimeSeriesData>& data_list);
    
    /**
     * @brief Query data from a specific table
     * @param table_name Source table name
     * @param config Query configuration
     * @return Vector of matching data points
     * 
     * Example:
     *   auto results = db.query("stream_s", QueryConfig{
     *       .time_range = {start, end},
     *       .filter_tags = {{"symbol", "AAPL"}}
     *   });
     */
    std::vector<TimeSeriesData> query(const std::string& table_name, 
                                       const QueryConfig& config) const;
    
    /**
     * @brief Query data from a specific table with time range
     * @param table_name Source table name
     * @param time_range Time range for query
     * @param filter_tags Optional tags to filter by
     * @return Vector of matching data points
     */
    std::vector<TimeSeriesData> query(const std::string& table_name,
                                       const TimeRange& time_range,
                                       const Tags& filter_tags = {}) const;
    
    // ========== Default Table API (backward compatible) ==========
    
    /**
     * @brief Add a single data point to default table
     * @param data Time series data
     * @return Index of added data
     */
    size_t add(const TimeSeriesData& data);
    
    /**
     * @brief Add data with timestamp and value
     * @param timestamp Timestamp in milliseconds
     * @param value Numeric value
     * @param tags Optional tags
     * @param fields Optional fields
     * @return Index of added data
     */
    size_t add(int64_t timestamp, double value,
               const Tags& tags = {},
               const Fields& fields = {});
    
    /**
     * @brief Add data with vector value
     * @param timestamp Timestamp in milliseconds
     * @param value Vector value
     * @param tags Optional tags
     * @param fields Optional fields
     * @return Index of added data
     */
    size_t add(int64_t timestamp, const std::vector<double>& value,
               const Tags& tags = {},
               const Fields& fields = {});
    
    /**
     * @brief Add multiple data points
     * @param data_list Vector of time series data
     * @return Vector of indices
     */
    std::vector<size_t> add_batch(const std::vector<TimeSeriesData>& data_list);
    
    /**
     * @brief Query time series data
     * @param config Query configuration
     * @return Vector of matching data points
     */
    std::vector<TimeSeriesData> query(const QueryConfig& config) const;
    
    /**
     * @brief Query with time range
     * @param time_range Time range for query
     * @param filter_tags Optional tags to filter by
     * @return Vector of matching data points
     */
    std::vector<TimeSeriesData> query(const TimeRange& time_range,
                                       const Tags& filter_tags = {}) const;
    
    /**
     * @brief Register an algorithm
     * @param name Algorithm name
     * @param algorithm Algorithm instance
     */
    void register_algorithm(const std::string& name,
                           std::shared_ptr<TimeSeriesAlgorithm> algorithm);
    
    /**
     * @brief Apply a registered algorithm
     * @param name Algorithm name
     * @param data Input data
     * @return Algorithm output
     */
    std::vector<TimeSeriesData> apply_algorithm(
        const std::string& name,
        const std::vector<TimeSeriesData>& data) const;
    
    /**
     * @brief Check if algorithm is registered
     * @param name Algorithm name
     * @return True if registered
     */
    bool has_algorithm(const std::string& name) const;
    
    /**
     * @brief Get list of registered algorithms
     * @return Vector of algorithm names
     */
    std::vector<std::string> list_algorithms() const;
    
    /**
     * @brief Get number of data points
     */
    size_t size() const;
    
    /**
     * @brief Check if database is empty
     */
    bool empty() const;
    
    /**
     * @brief Clear all data
     */
    void clear();
    
    /**
     * @brief Get database statistics
     * @return Statistics map
     */
    std::map<std::string, int64_t> get_stats() const;
    
    // ========== Persistence Methods ==========
    
    /**
     * @brief Save all data to disk
     * @param file_path Path to save file
     * @return true if successful
     */
    bool save_to_disk(const std::string& file_path);
    
    /**
     * @brief Load data from disk
     * @param file_path Path to load file
     * @param clear_existing If true, clears existing data before loading
     * @return true if successful
     */
    bool load_from_disk(const std::string& file_path, bool clear_existing = true);
    
    /**
     * @brief Create a checkpoint of current data
     * @param checkpoint_id Checkpoint identifier
     * @return true if successful
     */
    bool create_checkpoint(uint64_t checkpoint_id);
    
    /**
     * @brief Restore data from a checkpoint
     * @param checkpoint_id Checkpoint identifier to restore
     * @param clear_existing If true, clears existing data before restoring
     * @return true if successful
     */
    bool restore_from_checkpoint(uint64_t checkpoint_id, bool clear_existing = true);
    
    /**
     * @brief List all available checkpoints
     * @return Vector of checkpoint IDs and metadata
     */
    std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>> list_checkpoints() const;
    
    /**
     * @brief Delete a checkpoint
     * @param checkpoint_id Checkpoint identifier to delete
     * @return true if successful
     */
    bool delete_checkpoint(uint64_t checkpoint_id);
    
    /**
     * @brief Set storage base path
     * @param path Base directory for storage files
     */
    void set_storage_path(const std::string& path);
    
    /**
     * @brief Get storage base path
     * @return Base directory path
     */
    std::string get_storage_path() const;
    
    /**
     * @brief Enable/disable compression
     * @param enable true to enable compression
     */
    void set_compression_enabled(bool enable);
    
    /**
     * @brief Get storage statistics
     * @return Storage statistics map
     */
    std::map<std::string, uint64_t> get_storage_stats() const;
    
    // ========== Resource Management API ==========
    
    /**
     * @brief Get ResourceManager instance
     * @return Pointer to ResourceManager (shared across all components)
     */
    std::shared_ptr<ResourceManager> getResourceManager() const;
    
    /**
     * @brief Set ResourceManager instance
     * @param resource_manager ResourceManager to use
     * 
     * Note: Must be called before any compute engine initialization
     */
    void setResourceManager(std::shared_ptr<ResourceManager> resource_manager);

private:
    // Core index (default table)
    std::unique_ptr<TimeSeriesIndex> index_;
    
    // Multi-table storage: table_name -> index
    std::unordered_map<std::string, std::unique_ptr<TimeSeriesIndex>> tables_;
    
    // Table type metadata: table_name -> type
    std::unordered_map<std::string, TableType> table_types_;
    
    // Registered algorithms
    std::unordered_map<std::string, std::shared_ptr<TimeSeriesAlgorithm>> algorithms_;
    
    // Statistics
    mutable int64_t query_count_;
    mutable int64_t write_count_;
    
    // Storage engine (forward declared, initialized in constructor)
    std::unique_ptr<class StorageEngine> storage_engine_;
    
    // Resource manager (shared)
    std::shared_ptr<ResourceManager> resource_manager_;
    
    // Helper: Get or create table index
    TimeSeriesIndex* getTableIndex(const std::string& table_name);
    const TimeSeriesIndex* getTableIndex(const std::string& table_name) const;
};

} // namespace sage_tsdb
