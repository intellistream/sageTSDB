#pragma once

#include "algorithm_base.h"
#include <deque>
#include <functional>

namespace sage_tsdb {

/**
 * @brief Stream buffer for handling out-of-order data
 * 
 * Uses watermarking to handle late arrivals
 */
class StreamBuffer {
public:
    explicit StreamBuffer(int64_t max_delay = 5000);
    
    /**
     * @brief Add data to buffer
     */
    void add(const TimeSeriesData& data);
    
    /**
     * @brief Add multiple data points
     */
    void add_batch(const std::vector<TimeSeriesData>& data);
    
    /**
     * @brief Get data ready for processing (before watermark)
     * @return Ready data points (sorted by timestamp)
     */
    std::vector<TimeSeriesData> get_ready_data();
    
    /**
     * @brief Get current watermark
     */
    int64_t get_watermark() const { return watermark_; }
    
    /**
     * @brief Get buffer size
     */
    size_t size() const { return buffer_.size(); }
    
    /**
     * @brief Clear buffer
     */
    void clear();

private:
    void update_watermark();
    
    int64_t max_delay_;
    int64_t watermark_;
    std::deque<TimeSeriesData> buffer_;
};

/**
 * @brief Out-of-order stream join algorithm
 * 
 * Joins two time series streams based on time windows,
 * handling out-of-order arrivals using watermarking.
 * 
 * Configuration parameters:
 * - window_size: Join window size in milliseconds
 * - max_delay: Maximum out-of-order delay in milliseconds
 * - join_key: Optional tag key for equi-join
 */
class StreamJoin : public TimeSeriesAlgorithm {
public:
    using JoinPredicate = std::function<bool(
        const TimeSeriesData&, const TimeSeriesData&)>;
    
    explicit StreamJoin(const AlgorithmConfig& config = {});
    
    /**
     * @brief Process stream join (override)
     * @param input Not used (for compatibility)
     * @return Joined data pairs (flattened)
     */
    std::vector<TimeSeriesData> process(
        const std::vector<TimeSeriesData>& input) override;
    
    /**
     * @brief Process join with explicit left and right streams
     * @param left_stream Left stream data
     * @param right_stream Right stream data
     * @return Joined data pairs
     */
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> process_join(
        const std::vector<TimeSeriesData>& left_stream,
        const std::vector<TimeSeriesData>& right_stream);
    
    /**
     * @brief Set custom join predicate
     */
    void set_join_predicate(JoinPredicate predicate);
    
    /**
     * @brief Reset algorithm state
     */
    void reset() override;
    
    /**
     * @brief Get join statistics
     */
    std::map<std::string, int64_t> get_stats() const override;

private:
    /**
     * @brief Join ready data from both buffers
     */
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> join_data(
        const std::vector<TimeSeriesData>& left_data,
        const std::vector<TimeSeriesData>& right_data);
    
    /**
     * @brief Hash join on specified key
     */
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> hash_join(
        const std::vector<TimeSeriesData>& left_data,
        const std::vector<TimeSeriesData>& right_data);
    
    /**
     * @brief Nested loop join with window condition
     */
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> nested_loop_join(
        const std::vector<TimeSeriesData>& left_data,
        const std::vector<TimeSeriesData>& right_data);
    
    // Configuration
    int64_t window_size_;
    int64_t max_delay_;
    std::string join_key_;
    JoinPredicate join_predicate_;
    
    // Buffers
    StreamBuffer left_buffer_;
    StreamBuffer right_buffer_;
    
    // Statistics
    mutable int64_t total_joined_;
    mutable int64_t late_arrivals_;
    mutable int64_t dropped_late_;
};

} // namespace sage_tsdb
