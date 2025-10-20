#include "sage_tsdb/algorithms/window_aggregator.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace sage_tsdb {

WindowAggregator::WindowAggregator(const AlgorithmConfig& config)
    : TimeSeriesAlgorithm(config),
      windows_created_(0),
      windows_completed_(0),
      data_points_processed_(0) {
    
    // Parse configuration
    std::string window_type_str = get_config("window_type", "tumbling");
    if (window_type_str == "tumbling") {
        window_type_ = WindowType::TUMBLING;
    } else if (window_type_str == "sliding") {
        window_type_ = WindowType::SLIDING;
    } else if (window_type_str == "session") {
        window_type_ = WindowType::SESSION;
    } else {
        window_type_ = WindowType::TUMBLING;
    }
    
    window_size_ = std::stoll(get_config("window_size", "60000"));
    slide_interval_ = std::stoll(get_config("slide_interval", 
                                            std::to_string(window_size_)));
    session_gap_ = std::stoll(get_config("session_gap", "30000"));
    
    // Parse aggregation type
    std::string agg_str = get_config("aggregation", "avg");
    if (agg_str == "sum") {
        aggregation_ = AggregationType::SUM;
    } else if (agg_str == "avg") {
        aggregation_ = AggregationType::AVG;
    } else if (agg_str == "min") {
        aggregation_ = AggregationType::MIN;
    } else if (agg_str == "max") {
        aggregation_ = AggregationType::MAX;
    } else if (agg_str == "count") {
        aggregation_ = AggregationType::COUNT;
    } else {
        aggregation_ = AggregationType::AVG;
    }
}

std::vector<TimeSeriesData> WindowAggregator::process(
    const std::vector<TimeSeriesData>& input) {
    
    if (input.empty()) {
        return {};
    }
    
    // Sort input by timestamp
    auto sorted_data = input;
    std::sort(sorted_data.begin(), sorted_data.end(),
             [](const TimeSeriesData& a, const TimeSeriesData& b) {
                 return a.timestamp < b.timestamp;
             });
    
    // Apply windowing based on type
    switch (window_type_) {
        case WindowType::TUMBLING:
            return tumbling_window(sorted_data);
        case WindowType::SLIDING:
            return sliding_window(sorted_data);
        case WindowType::SESSION:
            return session_window(sorted_data);
        default:
            return {};
    }
}

void WindowAggregator::reset() {
    windows_created_ = 0;
    windows_completed_ = 0;
    data_points_processed_ = 0;
}

std::map<std::string, int64_t> WindowAggregator::get_stats() const {
    return {
        {"windows_created", windows_created_},
        {"windows_completed", windows_completed_},
        {"data_points_processed", data_points_processed_}
    };
}

std::vector<TimeSeriesData> WindowAggregator::tumbling_window(
    const std::vector<TimeSeriesData>& data) {
    
    std::vector<TimeSeriesData> results;
    std::vector<TimeSeriesData> window_data;
    
    int64_t window_start = align_to_window(data[0].timestamp);
    
    for (const auto& point : data) {
        int64_t point_window = align_to_window(point.timestamp);
        
        if (point_window == window_start) {
            window_data.push_back(point);
        } else {
            // Complete current window
            if (!window_data.empty()) {
                results.push_back(aggregate_window(window_data, window_start));
                ++windows_completed_;
            }
            
            // Advance to new window
            window_start = point_window;
            window_data = {point};
            ++windows_created_;
        }
    }
    
    // Complete last window
    if (!window_data.empty()) {
        results.push_back(aggregate_window(window_data, window_start));
        ++windows_completed_;
    }
    
    data_points_processed_ += data.size();
    return results;
}

std::vector<TimeSeriesData> WindowAggregator::sliding_window(
    const std::vector<TimeSeriesData>& data) {
    
    std::vector<TimeSeriesData> results;
    
    int64_t window_start = align_to_window(data[0].timestamp);
    int64_t last_timestamp = data.back().timestamp;
    
    while (window_start <= last_timestamp) {
        int64_t window_end = window_start + window_size_;
        
        // Collect data in this window
        std::vector<TimeSeriesData> window_data;
        for (const auto& point : data) {
            if (point.timestamp >= window_start && point.timestamp < window_end) {
                window_data.push_back(point);
            }
        }
        
        if (!window_data.empty()) {
            results.push_back(aggregate_window(window_data, window_start));
            ++windows_completed_;
        }
        
        // Slide to next window
        window_start += slide_interval_;
        ++windows_created_;
    }
    
    data_points_processed_ += data.size();
    return results;
}

std::vector<TimeSeriesData> WindowAggregator::session_window(
    const std::vector<TimeSeriesData>& data) {
    
    std::vector<TimeSeriesData> results;
    std::vector<TimeSeriesData> session_data;
    
    int64_t session_start = data[0].timestamp;
    int64_t last_timestamp = data[0].timestamp;
    
    for (const auto& point : data) {
        if (point.timestamp - last_timestamp <= session_gap_) {
            // Within session
            session_data.push_back(point);
        } else {
            // Complete current session
            if (!session_data.empty()) {
                results.push_back(aggregate_window(session_data, session_start));
                ++windows_completed_;
            }
            
            // Start new session
            session_data = {point};
            session_start = point.timestamp;
            ++windows_created_;
        }
        
        last_timestamp = point.timestamp;
    }
    
    // Complete last session
    if (!session_data.empty()) {
        results.push_back(aggregate_window(session_data, session_start));
        ++windows_completed_;
    }
    
    data_points_processed_ += data.size();
    return results;
}

TimeSeriesData WindowAggregator::aggregate_window(
    const std::vector<TimeSeriesData>& window_data,
    int64_t window_timestamp) {
    
    // Extract values
    std::vector<double> values;
    for (const auto& point : window_data) {
        if (point.is_scalar()) {
            values.push_back(point.as_double());
        } else {
            auto vec = point.as_vector();
            values.insert(values.end(), vec.begin(), vec.end());
        }
    }
    
    // Apply aggregation
    double agg_value = apply_aggregation(values);
    
    // Merge tags
    Tags merged_tags;
    for (const auto& point : window_data) {
        for (const auto& [key, value] : point.tags) {
            merged_tags[key] = value;
        }
    }
    
    // Create result
    TimeSeriesData result(window_timestamp, agg_value, merged_tags);
    result.fields["window_size"] = std::to_string(window_data.size());
    
    return result;
}

int64_t WindowAggregator::align_to_window(int64_t timestamp) const {
    return (timestamp / window_size_) * window_size_;
}

double WindowAggregator::apply_aggregation(const std::vector<double>& values) const {
    if (values.empty()) {
        return 0.0;
    }
    
    switch (aggregation_) {
        case AggregationType::SUM:
            return std::accumulate(values.begin(), values.end(), 0.0);
        
        case AggregationType::AVG: {
            double sum = std::accumulate(values.begin(), values.end(), 0.0);
            return sum / values.size();
        }
        
        case AggregationType::MIN:
            return *std::min_element(values.begin(), values.end());
        
        case AggregationType::MAX:
            return *std::max_element(values.begin(), values.end());
        
        case AggregationType::COUNT:
            return static_cast<double>(values.size());
        
        case AggregationType::FIRST:
            return values.front();
        
        case AggregationType::LAST:
            return values.back();
        
        case AggregationType::STDDEV: {
            double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            double sq_sum = 0.0;
            for (double v : values) {
                sq_sum += (v - mean) * (v - mean);
            }
            return std::sqrt(sq_sum / values.size());
        }
        
        default:
            return 0.0;
    }
}

} // namespace sage_tsdb
