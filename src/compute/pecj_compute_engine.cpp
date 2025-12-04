/**
 * @file pecj_compute_engine.cpp
 * @brief Implementation of PECJ Compute Engine for Deep Integration Mode
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-04
 */

#include "sage_tsdb/compute/pecj_compute_engine.h"

#ifdef PECJ_MODE_INTEGRATED

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <sstream>

// sageTSDB core headers (to be implemented)
#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/resource_handle.h"

// PECJ headers
#ifdef PECJ_FULL_INTEGRATION
#include "OoOJoin.h"
#include "Operator/AbstractOperator.h"
#include "Common/TrackTuple.h"
#endif

namespace sage_tsdb {
namespace compute {

namespace {
    // Constants
    constexpr size_t MAX_LATENCY_SAMPLES = 1000;  ///< Max samples for percentile calculation
    constexpr double MEMORY_CHECK_THRESHOLD = 0.9; ///< Memory warning threshold (90%)
    
    /**
     * @brief Calculate percentile from sorted samples
     */
    double calculatePercentile(const std::vector<double>& sorted_samples, double percentile) {
        if (sorted_samples.empty()) return 0.0;
        
        size_t index = static_cast<size_t>(sorted_samples.size() * percentile);
        if (index >= sorted_samples.size()) {
            index = sorted_samples.size() - 1;
        }
        return sorted_samples[index];
    }
    
    /**
     * @brief Get current timestamp in microseconds
     */
    int64_t getCurrentTimestampUs() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
    }
    
} // anonymous namespace

PECJComputeEngine::PECJComputeEngine()
    : db_(nullptr)
    , resource_handle_(nullptr)
    , initialized_(false)
    , current_memory_usage_(0) {
}

PECJComputeEngine::~PECJComputeEngine() {
    // Cleanup PECJ operator
    pecj_operator_.reset();
}

bool PECJComputeEngine::initialize(const ComputeConfig& config,
                                   core::TimeSeriesDB* db,
                                   core::ResourceHandle* resource_handle) {
    if (initialized_.load()) {
        return false; // Already initialized
    }
    
    if (!db || !resource_handle) {
        return false; // Invalid arguments
    }
    
    // Store configuration and references
    config_ = config;
    db_ = db;
    resource_handle_ = resource_handle;
    
    // Validate configuration
    if (!config_.time_range.valid()) {
        return false;
    }
    
    if (config_.window_len_us == 0 || config_.slide_len_us == 0) {
        return false;
    }
    
    // Create PECJ operator
    if (!createPECJOperator()) {
        return false;
    }
    
    // Initialize metrics
    metrics_ = ComputeMetrics{};
    latency_samples_.reserve(MAX_LATENCY_SAMPLES);
    
    initialized_.store(true);
    return true;
}

bool PECJComputeEngine::createPECJOperator() {
#ifdef PECJ_FULL_INTEGRATION
    try {
        // Configure PECJ operator based on config_
        OoOJoin::OperatorConfig pecj_config;
        pecj_config.window_len = config_.window_len_us;
        pecj_config.slide_len = config_.slide_len_us;
        pecj_config.max_delay = config_.max_delay_us;
        
        // Create operator based on type
        if (config_.operator_type == "IAWJ") {
            pecj_operator_ = std::make_unique<OoOJoin::IAWJOperator>(pecj_config);
        } else if (config_.operator_type == "MWAY") {
            pecj_operator_ = std::make_unique<OoOJoin::MWAYOperator>(pecj_config);
        } else {
            // Default to IAWJ
            pecj_operator_ = std::make_unique<OoOJoin::IAWJOperator>(pecj_config);
        }
        
        return pecj_operator_ != nullptr;
        
    } catch (const std::exception& e) {
        // Log error
        return false;
    }
#else
    // Stub mode: no actual PECJ operator
    return true;
#endif
}

ComputeStatus PECJComputeEngine::executeWindowJoin(uint64_t window_id,
                                                    const TimeRange& time_range) {
    if (!initialized_.load()) {
        return ComputeStatus{
            .success = false,
            .error = "Engine not initialized",
            .window_id = window_id
        };
    }
    
    if (!time_range.valid()) {
        return ComputeStatus{
            .success = false,
            .error = "Invalid time range",
            .window_id = window_id
        };
    }
    
    auto start_time = std::chrono::steady_clock::now();
    ComputeStatus status;
    status.window_id = window_id;
    
    try {
        // Step 1: Query data from sageTSDB tables
        std::vector<std::vector<uint8_t>> s_data_raw;
        std::vector<std::vector<uint8_t>> r_data_raw;
        
        // Query Stream S
        if (!db_->query(config_.stream_s_table, time_range, s_data_raw)) {
            status.success = false;
            status.error = "Failed to query stream_s table";
            return status;
        }
        
        // Query Stream R
        if (!db_->query(config_.stream_r_table, time_range, r_data_raw)) {
            status.success = false;
            status.error = "Failed to query stream_r table";
            return status;
        }
        
        status.input_s_count = s_data_raw.size();
        status.input_r_count = r_data_raw.size();
        
        // Step 2: Convert to PECJ format
        auto s_data = convertFromTable(s_data_raw);
        auto r_data = convertFromTable(r_data_raw);
        
        // Step 3: Check memory before computation
        if (!checkMemoryLimit()) {
            status.success = false;
            status.error = "Memory limit exceeded";
            return status;
        }
        
        // Step 4: Execute PECJ join with timeout protection
        status = executeWithTimeout(s_data, r_data, window_id, time_range);
        
        // Step 5: Calculate metrics
        auto end_time = std::chrono::steady_clock::now();
        status.computation_time_ms = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
        
        if (status.input_s_count > 0 && status.input_r_count > 0) {
            status.selectivity = static_cast<double>(status.join_count) /
                               (status.input_s_count * status.input_r_count);
        }
        
        // Step 6: Update global metrics
        updateMetrics(status);
        
    } catch (const std::exception& e) {
        status.success = false;
        status.error = std::string("Exception: ") + e.what();
    }
    
    return status;
}

ComputeStatus PECJComputeEngine::executeWithTimeout(
    const std::vector<OoOJoin::TrackTuple>& s_data,
    const std::vector<OoOJoin::TrackTuple>& r_data,
    uint64_t window_id,
    const TimeRange& time_range) {
    
    ComputeStatus status;
    status.window_id = window_id;
    status.input_s_count = s_data.size();
    status.input_r_count = r_data.size();
    
#ifdef PECJ_FULL_INTEGRATION
    try {
        auto start = std::chrono::steady_clock::now();
        
        // Execute PECJ join
        std::vector<std::pair<OoOJoin::TrackTuple, OoOJoin::TrackTuple>> join_results;
        
        // TODO: Implement timeout mechanism using resource_handle_->submitTask()
        // For now, execute directly
        if (pecj_operator_) {
            join_results = pecj_operator_->join(s_data, r_data);
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        // Check timeout
        if (duration_ms > config_.timeout_ms) {
            status.timeout_occurred = true;
            
            // Fallback to AQP if enabled
            if (config_.enable_aqp) {
                return fallbackToAQP(s_data, r_data, window_id);
            }
        }
        
        // Convert results to sageTSDB format
        auto result_data = convertToTable(join_results);
        
        // Write to result table
        if (!writeResults(window_id, result_data, status)) {
            status.success = false;
            status.error = "Failed to write results to table";
            return status;
        }
        
        status.success = true;
        status.join_count = join_results.size();
        status.used_aqp = false;
        
    } catch (const std::exception& e) {
        status.success = false;
        status.error = std::string("PECJ execution failed: ") + e.what();
    }
#else
    // Stub mode: return mock results
    status.success = true;
    status.join_count = 0;
    status.used_aqp = false;
#endif
    
    return status;
}

ComputeStatus PECJComputeEngine::fallbackToAQP(
    const std::vector<OoOJoin::TrackTuple>& s_data,
    const std::vector<OoOJoin::TrackTuple>& r_data,
    uint64_t window_id) {
    
    ComputeStatus status;
    status.window_id = window_id;
    status.input_s_count = s_data.size();
    status.input_r_count = r_data.size();
    status.used_aqp = true;
    
#ifdef PECJ_FULL_INTEGRATION
    try {
        // Simple AQP estimation: use sampling
        double sample_ratio = 0.1; // Sample 10%
        size_t s_sample_size = std::max(size_t(1), size_t(s_data.size() * sample_ratio));
        size_t r_sample_size = std::max(size_t(1), size_t(r_data.size() * sample_ratio));
        
        // Execute join on sample
        std::vector<OoOJoin::TrackTuple> s_sample(s_data.begin(), 
                                                   s_data.begin() + s_sample_size);
        std::vector<OoOJoin::TrackTuple> r_sample(r_data.begin(),
                                                   r_data.begin() + r_sample_size);
        
        auto sample_results = pecj_operator_->join(s_sample, r_sample);
        
        // Estimate full result count
        double scaling_factor = 1.0 / (sample_ratio * sample_ratio);
        status.aqp_estimate = sample_results.size() * scaling_factor;
        status.join_count = static_cast<size_t>(status.aqp_estimate);
        
        // Convert and write results
        auto result_data = convertToTable(sample_results);
        writeResults(window_id, result_data, status);
        
        status.success = true;
        
        // Update AQP metrics
        {
            std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
            metrics_.aqp_invocations++;
        }
        
    } catch (const std::exception& e) {
        status.success = false;
        status.error = std::string("AQP fallback failed: ") + e.what();
    }
#else
    status.success = true;
    status.aqp_estimate = 0.0;
#endif
    
    return status;
}

std::vector<OoOJoin::TrackTuple> PECJComputeEngine::convertFromTable(
    const std::vector<std::vector<uint8_t>>& db_data) {
    
    std::vector<OoOJoin::TrackTuple> result;
    result.reserve(db_data.size());
    
#ifdef PECJ_FULL_INTEGRATION
    for (const auto& raw_data : db_data) {
        if (raw_data.size() < sizeof(OoOJoin::TrackTuple)) {
            continue; // Skip invalid data
        }
        
        // Deserialize from bytes
        OoOJoin::TrackTuple tuple;
        std::memcpy(&tuple, raw_data.data(), sizeof(OoOJoin::TrackTuple));
        result.push_back(tuple);
    }
#endif
    
    return result;
}

std::vector<std::vector<uint8_t>> PECJComputeEngine::convertToTable(
    const std::vector<std::pair<OoOJoin::TrackTuple, OoOJoin::TrackTuple>>& pecj_result) {
    
    std::vector<std::vector<uint8_t>> result;
    result.reserve(pecj_result.size());
    
#ifdef PECJ_FULL_INTEGRATION
    for (const auto& [s_tuple, r_tuple] : pecj_result) {
        // Serialize join result to bytes
        // Format: [s_tuple][r_tuple]
        std::vector<uint8_t> data(sizeof(OoOJoin::TrackTuple) * 2);
        
        std::memcpy(data.data(), &s_tuple, sizeof(OoOJoin::TrackTuple));
        std::memcpy(data.data() + sizeof(OoOJoin::TrackTuple), 
                   &r_tuple, sizeof(OoOJoin::TrackTuple));
        
        result.push_back(std::move(data));
    }
#endif
    
    return result;
}

bool PECJComputeEngine::writeResults(uint64_t window_id,
                                     const std::vector<std::vector<uint8_t>>& results,
                                     const ComputeStatus& status) {
    try {
        // Write each result tuple to the result table
        for (const auto& result : results) {
            if (!db_->insert(config_.result_table, window_id, result)) {
                return false;
            }
        }
        
        // Write metadata about the window
        std::vector<uint8_t> metadata;
        // Serialize status information
        // Format: [window_id][join_count][computation_time][...]
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

void PECJComputeEngine::updateMetrics(const ComputeStatus& status) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    if (status.success) {
        metrics_.total_windows_completed++;
        metrics_.total_tuples_processed += status.input_s_count + status.input_r_count;
    } else {
        metrics_.failed_windows++;
    }
    
    if (status.timeout_occurred) {
        metrics_.timeout_windows++;
    }
    
    // Update latency statistics
    if (status.computation_time_ms > 0) {
        latency_samples_.push_back(status.computation_time_ms);
        
        // Keep only recent samples
        if (latency_samples_.size() > MAX_LATENCY_SAMPLES) {
            latency_samples_.erase(latency_samples_.begin());
        }
        
        // Calculate statistics
        auto sorted_samples = latency_samples_;
        std::sort(sorted_samples.begin(), sorted_samples.end());
        
        metrics_.avg_window_latency_ms = std::accumulate(
            sorted_samples.begin(), sorted_samples.end(), 0.0) / sorted_samples.size();
        
        metrics_.min_window_latency_ms = sorted_samples.front();
        metrics_.max_window_latency_ms = sorted_samples.back();
        metrics_.p99_window_latency_ms = calculatePercentile(sorted_samples, 0.99);
    }
    
    // Update selectivity
    if (status.selectivity > 0) {
        double total_selectivity = metrics_.avg_join_selectivity * 
                                  (metrics_.total_windows_completed - 1);
        metrics_.avg_join_selectivity = 
            (total_selectivity + status.selectivity) / metrics_.total_windows_completed;
    }
    
    // Update memory
    metrics_.peak_memory_bytes = std::max(metrics_.peak_memory_bytes, 
                                         status.memory_used_bytes);
    
    // Update AQP metrics
    if (status.used_aqp && status.aqp_error > 0) {
        double total_error = metrics_.avg_aqp_error_rate * 
                           (metrics_.aqp_invocations - 1);
        metrics_.avg_aqp_error_rate = 
            (total_error + status.aqp_error) / metrics_.aqp_invocations;
    }
}

ComputeMetrics PECJComputeEngine::getMetrics() const {
    std::shared_lock<std::shared_mutex> lock(metrics_mutex_);
    return metrics_;
}

void PECJComputeEngine::reset() {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    
    // Reset metrics
    metrics_ = ComputeMetrics{};
    latency_samples_.clear();
    current_memory_usage_.store(0);
    
    // Note: We do NOT reset the PECJ operator or clear data in tables
    // Those are managed by sageTSDB
}

bool PECJComputeEngine::checkMemoryLimit() const {
    size_t current = current_memory_usage_.load();
    size_t limit = config_.max_memory_bytes;
    
    if (current >= limit) {
        return false;
    }
    
    // Warn if approaching limit
    if (static_cast<double>(current) / limit > MEMORY_CHECK_THRESHOLD) {
        // Log warning
    }
    
    return true;
}

} // namespace compute
} // namespace sage_tsdb

#endif // PECJ_MODE_INTEGRATED
