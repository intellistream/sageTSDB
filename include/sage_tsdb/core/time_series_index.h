#pragma once

#include "time_series_data.h"
#include <algorithm>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace sage_tsdb {

/**
 * @brief Index structure for efficient time series queries
 * 
 * Provides:
 * - Fast binary search by timestamp
 * - Tag-based indexing for filtering
 * - Automatic sorting for out-of-order data
 * - Thread-safe operations with read-write locks
 */
class TimeSeriesIndex {
public:
    TimeSeriesIndex();
    ~TimeSeriesIndex() = default;
    
    /**
     * @brief Add a single data point
     * @param data Time series data point
     * @return Index of added data
     */
    size_t add(const TimeSeriesData& data);
    
    /**
     * @brief Add multiple data points
     * @param data_list Vector of time series data
     * @return Vector of indices
     */
    std::vector<size_t> add_batch(const std::vector<TimeSeriesData>& data_list);
    
    /**
     * @brief Query data within time range
     * @param config Query configuration
     * @return Vector of matching data points
     */
    std::vector<TimeSeriesData> query(const QueryConfig& config) const;
    
    /**
     * @brief Get data point by index
     * @param index Data point index
     * @return Time series data
     */
    TimeSeriesData get(size_t index) const;
    
    /**
     * @brief Get number of data points
     */
    size_t size() const;
    
    /**
     * @brief Check if index is empty
     */
    bool empty() const;
    
    /**
     * @brief Clear all data
     */
    void clear();

private:
    /**
     * @brief Ensure data is sorted by timestamp
     */
    void ensure_sorted();
    
    /**
     * @brief Binary search for timestamp
     * @param timestamp Target timestamp
     * @param find_upper If true, find upper bound; else lower bound
     * @return Index position
     */
    size_t binary_search(int64_t timestamp, bool find_upper = false) const;
    
    /**
     * @brief Filter data by tags
     * @param tags Filter tags
     * @return Set of matching indices
     */
    std::vector<size_t> filter_by_tags(const Tags& tags) const;
    
    /**
     * @brief Rebuild tag index after sorting
     */
    void rebuild_tag_index();

    // Data storage
    std::vector<TimeSeriesData> data_;
    
    // Tag index: tag_key -> {tag_value -> [indices]}
    std::map<std::string, std::map<std::string, std::vector<size_t>>> tag_index_;
    
    // Sorted flag
    bool sorted_;
    
    // Thread safety
    mutable std::shared_mutex mutex_;
};

} // namespace sage_tsdb
