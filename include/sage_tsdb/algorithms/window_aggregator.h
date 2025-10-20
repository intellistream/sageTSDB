#pragma once

#include "algorithm_base.h"

namespace sage_tsdb {

/**
 * @brief Window types for aggregation
 */
enum class WindowType {
    TUMBLING,  // Non-overlapping fixed-size windows
    SLIDING,   // Overlapping fixed-size windows
    SESSION    // Dynamic windows based on inactivity gap
};

/**
 * @brief Window aggregator algorithm
 * 
 * Performs window-based aggregation on time series data.
 * Supports tumbling, sliding, and session windows.
 * 
 * Configuration parameters:
 * - window_type: Type of window (tumbling/sliding/session)
 * - window_size: Window size in milliseconds
 * - slide_interval: Slide interval for sliding windows (ms)
 * - session_gap: Inactivity gap for session windows (ms)
 * - aggregation: Aggregation function (sum/avg/min/max/count/etc.)
 */
class WindowAggregator : public TimeSeriesAlgorithm {
public:
    explicit WindowAggregator(const AlgorithmConfig& config = {});
    
    /**
     * @brief Process window aggregation
     * @param input Input time series data
     * @return Aggregated data (one point per window)
     */
    std::vector<TimeSeriesData> process(
        const std::vector<TimeSeriesData>& input) override;
    
    /**
     * @brief Reset algorithm state
     */
    void reset() override;
    
    /**
     * @brief Get aggregator statistics
     */
    std::map<std::string, int64_t> get_stats() const override;

private:
    /**
     * @brief Process with tumbling windows
     */
    std::vector<TimeSeriesData> tumbling_window(
        const std::vector<TimeSeriesData>& data);
    
    /**
     * @brief Process with sliding windows
     */
    std::vector<TimeSeriesData> sliding_window(
        const std::vector<TimeSeriesData>& data);
    
    /**
     * @brief Process with session windows
     */
    std::vector<TimeSeriesData> session_window(
        const std::vector<TimeSeriesData>& data);
    
    /**
     * @brief Aggregate data in a window
     */
    TimeSeriesData aggregate_window(
        const std::vector<TimeSeriesData>& window_data,
        int64_t window_timestamp);
    
    /**
     * @brief Align timestamp to window boundary
     */
    int64_t align_to_window(int64_t timestamp) const;
    
    /**
     * @brief Apply aggregation function
     */
    double apply_aggregation(const std::vector<double>& values) const;
    
    // Configuration
    WindowType window_type_;
    int64_t window_size_;
    int64_t slide_interval_;
    int64_t session_gap_;
    AggregationType aggregation_;
    
    // Statistics
    mutable int64_t windows_created_;
    mutable int64_t windows_completed_;
    mutable int64_t data_points_processed_;
};

} // namespace sage_tsdb
