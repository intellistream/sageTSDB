#pragma once

#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace sage_tsdb {

/**
 * @brief Value type for time series data
 * 
 * Supports single values, arrays, and complex structures
 */
using TimeSeriesValue = std::variant<double, std::vector<double>>;

/**
 * @brief Tags for time series data (string key-value pairs)
 */
using Tags = std::map<std::string, std::string>;

/**
 * @brief Fields for additional metadata
 */
using Fields = std::map<std::string, std::string>;

/**
 * @brief Time series data point
 * 
 * Represents a single observation in a time series with:
 * - timestamp: milliseconds since Unix epoch
 * - value: numeric value or array
 * - tags: indexable string key-value pairs
 * - fields: additional metadata
 */
struct TimeSeriesData {
    int64_t timestamp;        // milliseconds since epoch
    TimeSeriesValue value;    // numeric value or array
    Tags tags;                // indexable tags
    Fields fields;            // additional fields
    
    TimeSeriesData() : timestamp(0), value(0.0) {}
    
    TimeSeriesData(int64_t ts, double val)
        : timestamp(ts), value(val) {}
    
    TimeSeriesData(int64_t ts, const std::vector<double>& val)
        : timestamp(ts), value(val) {}
    
    TimeSeriesData(int64_t ts, double val, const Tags& t)
        : timestamp(ts), value(val), tags(t) {}
    
    TimeSeriesData(int64_t ts, const std::vector<double>& val, const Tags& t)
        : timestamp(ts), value(val), tags(t) {}
    
    /**
     * @brief Get value as double (first element if array)
     */
    double as_double() const;
    
    /**
     * @brief Get value as vector
     */
    std::vector<double> as_vector() const;
    
    /**
     * @brief Check if value is scalar
     */
    bool is_scalar() const {
        return std::holds_alternative<double>(value);
    }
    
    /**
     * @brief Check if value is array
     */
    bool is_array() const {
        return std::holds_alternative<std::vector<double>>(value);
    }
};

/**
 * @brief Time range for queries
 */
struct TimeRange {
    int64_t start_time;  // inclusive
    int64_t end_time;    // inclusive
    
    TimeRange() : start_time(0), end_time(0) {}
    
    TimeRange(int64_t start, int64_t end)
        : start_time(start), end_time(end) {}
    
    bool contains(int64_t timestamp) const {
        return timestamp >= start_time && timestamp <= end_time;
    }
    
    int64_t duration() const {
        return end_time - start_time;
    }
};

/**
 * @brief Aggregation types for time series
 */
enum class AggregationType {
    NONE = -1,
    SUM = 0,
    AVG = 1,
    MIN = 2,
    MAX = 3,
    COUNT = 4,
    FIRST = 5,
    LAST = 6,
    STDDEV = 7
};

/**
 * @brief Query configuration
 */
struct QueryConfig {
    TimeRange time_range;
    Tags filter_tags;
    AggregationType aggregation = AggregationType::NONE;
    int64_t window_size = 0;  // milliseconds, 0 means no windowing
    int32_t limit = 1000;     // default limit
    
    QueryConfig() = default;
    
    QueryConfig(const TimeRange& range)
        : time_range(range) {}
    
    QueryConfig(const TimeRange& range, const Tags& tags)
        : time_range(range), filter_tags(tags) {}
};

/**
 * @brief Convert aggregation type to string
 */
inline std::string aggregation_to_string(AggregationType type) {
    switch (type) {
        case AggregationType::SUM: return "sum";
        case AggregationType::AVG: return "avg";
        case AggregationType::MIN: return "min";
        case AggregationType::MAX: return "max";
        case AggregationType::COUNT: return "count";
        case AggregationType::FIRST: return "first";
        case AggregationType::LAST: return "last";
        case AggregationType::STDDEV: return "stddev";
        case AggregationType::NONE: return "none";
        default: return "unknown";
    }
}

/**
 * @brief Convert string to aggregation type
 */
inline AggregationType string_to_aggregation(const std::string& str) {
    std::string lower = str;
    // Convert to lowercase
    for (auto& c : lower) {
        c = std::tolower(c);
    }
    
    if (lower == "sum") return AggregationType::SUM;
    if (lower == "avg") return AggregationType::AVG;
    if (lower == "min") return AggregationType::MIN;
    if (lower == "max") return AggregationType::MAX;
    if (lower == "count") return AggregationType::COUNT;
    if (lower == "first") return AggregationType::FIRST;
    if (lower == "last") return AggregationType::LAST;
    if (lower == "stddev") return AggregationType::STDDEV;
    if (lower == "none") return AggregationType::NONE;
    
    throw std::invalid_argument("Unknown aggregation type: " + str);
}

} // namespace sage_tsdb
