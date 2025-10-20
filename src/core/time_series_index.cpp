#include "sage_tsdb/core/time_series_index.h"
#include <algorithm>
#include <set>

namespace sage_tsdb {

TimeSeriesIndex::TimeSeriesIndex()
    : sorted_(true) {}

size_t TimeSeriesIndex::add(const TimeSeriesData& data) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    size_t idx = data_.size();
    data_.push_back(data);
    
    // Update tag index
    for (const auto& [key, value] : data.tags) {
        tag_index_[key][value].push_back(idx);
    }
    
    // Check if data is out of order
    if (idx > 0 && data.timestamp < data_[idx - 1].timestamp) {
        sorted_ = false;
    }
    
    return idx;
}

std::vector<size_t> TimeSeriesIndex::add_batch(
    const std::vector<TimeSeriesData>& data_list) {
    std::vector<size_t> indices;
    indices.reserve(data_list.size());
    
    for (const auto& data : data_list) {
        indices.push_back(add(data));
    }
    
    return indices;
}

std::vector<TimeSeriesData> TimeSeriesIndex::query(
    const QueryConfig& config) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    // Ensure data is sorted (may need to upgrade lock)
    if (!sorted_) {
        lock.unlock();
        const_cast<TimeSeriesIndex*>(this)->ensure_sorted();
        lock.lock();
    }
    
    // Binary search for time range
    size_t start_idx = binary_search(config.time_range.start_time);
    size_t end_idx = binary_search(config.time_range.end_time, true);
    
    // Filter by tags if specified
    std::vector<TimeSeriesData> results;
    
    if (!config.filter_tags.empty()) {
        auto matching_indices = filter_by_tags(config.filter_tags);
        
        // Collect data points in time range that match tags
        for (size_t i = start_idx; i <= end_idx && i < data_.size(); ++i) {
            if (std::find(matching_indices.begin(), matching_indices.end(), i) 
                != matching_indices.end()) {
                results.push_back(data_[i]);
                if (config.limit > 0 && 
                    results.size() >= static_cast<size_t>(config.limit)) {
                    break;
                }
            }
        }
    } else {
        // No tag filtering
        for (size_t i = start_idx; i <= end_idx && i < data_.size(); ++i) {
            results.push_back(data_[i]);
            if (config.limit > 0 && 
                results.size() >= static_cast<size_t>(config.limit)) {
                break;
            }
        }
    }
    
    return results;
}

TimeSeriesData TimeSeriesIndex::get(size_t index) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    if (index >= data_.size()) {
        throw std::out_of_range("Index out of range");
    }
    
    return data_[index];
}

size_t TimeSeriesIndex::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.size();
}

bool TimeSeriesIndex::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.empty();
}

void TimeSeriesIndex::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    data_.clear();
    tag_index_.clear();
    sorted_ = true;
}

void TimeSeriesIndex::ensure_sorted() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (!sorted_) {
        // Sort data by timestamp
        std::sort(data_.begin(), data_.end(),
                 [](const TimeSeriesData& a, const TimeSeriesData& b) {
                     return a.timestamp < b.timestamp;
                 });
        
        // Rebuild tag index
        rebuild_tag_index();
        sorted_ = true;
    }
}

size_t TimeSeriesIndex::binary_search(int64_t timestamp, bool find_upper) const {
    if (data_.empty()) {
        return 0;
    }
    
    size_t left = 0;
    size_t right = data_.size() - 1;
    size_t result = find_upper ? right : 0;
    
    while (left <= right) {
        size_t mid = left + (right - left) / 2;
        int64_t mid_time = data_[mid].timestamp;
        
        if (mid_time < timestamp) {
            left = mid + 1;
            if (!find_upper) {
                result = left;
            }
        } else if (mid_time > timestamp) {
            if (mid == 0) break;
            right = mid - 1;
            if (find_upper) {
                result = right;
            }
        } else {
            return mid;
        }
    }
    
    // Clamp result to valid range
    if (result >= data_.size()) {
        result = data_.size() - 1;
    }
    
    return result;
}

std::vector<size_t> TimeSeriesIndex::filter_by_tags(const Tags& tags) const {
    if (tags.empty()) {
        return {};
    }
    
    std::vector<std::set<size_t>> matching_sets;
    
    for (const auto& [key, value] : tags) {
        auto key_it = tag_index_.find(key);
        if (key_it != tag_index_.end()) {
            auto value_it = key_it->second.find(value);
            if (value_it != key_it->second.end()) {
                matching_sets.emplace_back(
                    value_it->second.begin(), value_it->second.end());
            } else {
                // Tag value not found
                return {};
            }
        } else {
            // Tag key not found
            return {};
        }
    }
    
    if (matching_sets.empty()) {
        return {};
    }
    
    // Intersect all sets
    std::set<size_t> result = matching_sets[0];
    for (size_t i = 1; i < matching_sets.size(); ++i) {
        std::set<size_t> intersection;
        std::set_intersection(
            result.begin(), result.end(),
            matching_sets[i].begin(), matching_sets[i].end(),
            std::inserter(intersection, intersection.begin()));
        result = std::move(intersection);
    }
    
    return std::vector<size_t>(result.begin(), result.end());
}

void TimeSeriesIndex::rebuild_tag_index() {
    tag_index_.clear();
    
    for (size_t idx = 0; idx < data_.size(); ++idx) {
        for (const auto& [key, value] : data_[idx].tags) {
            tag_index_[key][value].push_back(idx);
        }
    }
}

} // namespace sage_tsdb
