#include "sage_tsdb/algorithms/stream_join.h"
#include <algorithm>
#include <unordered_map>

namespace sage_tsdb {

// StreamBuffer implementation
StreamBuffer::StreamBuffer(int64_t max_delay)
    : max_delay_(max_delay), watermark_(0) {}

void StreamBuffer::add(const TimeSeriesData& data) {
    buffer_.push_back(data);
    update_watermark();
}

void StreamBuffer::add_batch(const std::vector<TimeSeriesData>& data) {
    for (const auto& d : data) {
        buffer_.push_back(d);
    }
    update_watermark();
}

std::vector<TimeSeriesData> StreamBuffer::get_ready_data() {
    std::vector<TimeSeriesData> ready;
    
    // Sort buffer
    std::sort(buffer_.begin(), buffer_.end(),
             [](const TimeSeriesData& a, const TimeSeriesData& b) {
                 return a.timestamp < b.timestamp;
             });
    
    // Extract ready data (before watermark)
    auto it = buffer_.begin();
    while (it != buffer_.end() && it->timestamp <= watermark_) {
        ready.push_back(*it);
        ++it;
    }
    
    // Remove ready data from buffer
    buffer_.erase(buffer_.begin(), it);
    
    return ready;
}

void StreamBuffer::clear() {
    buffer_.clear();
    watermark_ = 0;
}

void StreamBuffer::update_watermark() {
    if (!buffer_.empty()) {
        // Find latest timestamp
        int64_t max_ts = buffer_[0].timestamp;
        for (const auto& data : buffer_) {
            if (data.timestamp > max_ts) {
                max_ts = data.timestamp;
            }
        }
        watermark_ = max_ts - max_delay_;
    }
}

// StreamJoin implementation
StreamJoin::StreamJoin(const AlgorithmConfig& config)
    : TimeSeriesAlgorithm(config),
      left_buffer_(5000),
      right_buffer_(5000),
      total_joined_(0),
      late_arrivals_(0),
      dropped_late_(0) {
    
    // Parse configuration
    window_size_ = std::stoll(get_config("window_size", "10000"));
    max_delay_ = std::stoll(get_config("max_delay", "5000"));
    join_key_ = get_config("join_key", "");
    
    // Update buffers with configured max_delay
    left_buffer_ = StreamBuffer(max_delay_);
    right_buffer_ = StreamBuffer(max_delay_);
}

std::vector<TimeSeriesData> StreamJoin::process(
    const std::vector<TimeSeriesData>& input) {
    // For compatibility - return empty
    // Use process_join for actual join operation
    return {};
}

std::vector<std::pair<TimeSeriesData, TimeSeriesData>> StreamJoin::process_join(
    const std::vector<TimeSeriesData>& left_stream,
    const std::vector<TimeSeriesData>& right_stream) {
    
    // Add data to buffers
    left_buffer_.add_batch(left_stream);
    right_buffer_.add_batch(right_stream);
    
    // Get ready data
    auto left_ready = left_buffer_.get_ready_data();
    auto right_ready = right_buffer_.get_ready_data();
    
    // Perform join
    auto joined = join_data(left_ready, right_ready);
    
    total_joined_ += joined.size();
    
    return joined;
}

void StreamJoin::set_join_predicate(JoinPredicate predicate) {
    join_predicate_ = predicate;
}

void StreamJoin::reset() {
    left_buffer_.clear();
    right_buffer_.clear();
    total_joined_ = 0;
    late_arrivals_ = 0;
    dropped_late_ = 0;
}

std::map<std::string, int64_t> StreamJoin::get_stats() const {
    return {
        {"total_joined", total_joined_},
        {"late_arrivals", late_arrivals_},
        {"dropped_late", dropped_late_},
        {"left_buffer_size", static_cast<int64_t>(left_buffer_.size())},
        {"right_buffer_size", static_cast<int64_t>(right_buffer_.size())},
        {"left_watermark", left_buffer_.get_watermark()},
        {"right_watermark", right_buffer_.get_watermark()}
    };
}

std::vector<std::pair<TimeSeriesData, TimeSeriesData>> StreamJoin::join_data(
    const std::vector<TimeSeriesData>& left_data,
    const std::vector<TimeSeriesData>& right_data) {
    
    if (!join_key_.empty()) {
        return hash_join(left_data, right_data);
    } else {
        return nested_loop_join(left_data, right_data);
    }
}

std::vector<std::pair<TimeSeriesData, TimeSeriesData>> StreamJoin::hash_join(
    const std::vector<TimeSeriesData>& left_data,
    const std::vector<TimeSeriesData>& right_data) {
    
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> joined;
    
    // Build hash table for right stream
    std::unordered_map<std::string, std::vector<TimeSeriesData>> right_hash;
    for (const auto& right : right_data) {
        auto it = right.tags.find(join_key_);
        if (it != right.tags.end()) {
            right_hash[it->second].push_back(right);
        }
    }
    
    // Probe with left stream
    for (const auto& left : left_data) {
        auto it = left.tags.find(join_key_);
        if (it != left.tags.end()) {
            auto hash_it = right_hash.find(it->second);
            if (hash_it != right_hash.end()) {
                for (const auto& right : hash_it->second) {
                    // Check window condition
                    if (std::abs(left.timestamp - right.timestamp) <= window_size_) {
                        // Check custom predicate if provided
                        if (!join_predicate_ || join_predicate_(left, right)) {
                            joined.emplace_back(left, right);
                        }
                    }
                }
            }
        }
    }
    
    return joined;
}

std::vector<std::pair<TimeSeriesData, TimeSeriesData>> StreamJoin::nested_loop_join(
    const std::vector<TimeSeriesData>& left_data,
    const std::vector<TimeSeriesData>& right_data) {
    
    std::vector<std::pair<TimeSeriesData, TimeSeriesData>> joined;
    
    for (const auto& left : left_data) {
        for (const auto& right : right_data) {
            // Check window condition
            if (std::abs(left.timestamp - right.timestamp) <= window_size_) {
                // Check custom predicate if provided
                if (!join_predicate_ || join_predicate_(left, right)) {
                    joined.emplace_back(left, right);
                }
            }
        }
    }
    
    return joined;
}

} // namespace sage_tsdb
