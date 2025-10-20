#pragma once

#include "time_series_data.h"
#include "time_series_index.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace sage_tsdb {

// Forward declaration
class TimeSeriesAlgorithm;

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
    ~TimeSeriesDB() = default;
    
    /**
     * @brief Add a single data point
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

private:
    // Core index
    std::unique_ptr<TimeSeriesIndex> index_;
    
    // Registered algorithms
    std::unordered_map<std::string, std::shared_ptr<TimeSeriesAlgorithm>> algorithms_;
    
    // Statistics
    mutable int64_t query_count_;
    mutable int64_t write_count_;
};

} // namespace sage_tsdb
