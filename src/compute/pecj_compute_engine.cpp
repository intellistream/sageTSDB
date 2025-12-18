/**
 * @file pecj_compute_engine.cpp
 * @brief Implementation of PECJ Compute Engine for Deep Integration Mode
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-04
 */

#ifdef PECJ_MODE_INTEGRATED

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <sstream>
#include <mutex>
#include <numeric>
#include <memory>

// sageTSDB core headers - include BEFORE everything else
#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/time_series_data.h"
#include "sage_tsdb/core/resource_manager.h"

// Now include the header - after core dependencies are resolved
#include "sage_tsdb/compute/pecj_compute_engine.h"

// PECJ needs std namespace visible for its macros
using namespace std;

// PECJ headers
#ifdef PECJ_FULL_INTEGRATION
#include "OoOJoin.h"
#include "Operator/IAWJOperator.h"
#include "Operator/MSWJOperator.h"
#include "Operator/RawSHJOperator.h"
#include "Operator/RawPRJOperator.h"
#include "Operator/MeanAQPIAWJOperator.h"
#include "Operator/IMAIAWJOperator.h"
#include "Operator/IAWJSelOperator.h"
#include "Operator/LazyIAWJSelOperator.h"
#include "Operator/AIOperator.h"
#include "Operator/LinearSVIOperator.h"
#include "Operator/PECJOperator.h"
#include "Common/Tuples.h"
#include <sys/time.h>
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

#ifdef PECJ_FULL_INTEGRATION
PECJComputeEngine::~PECJComputeEngine() {
    // Cleanup PECJ operator (requires complete type)
    pecj_operator_.reset();
}
#else
PECJComputeEngine::~PECJComputeEngine() {
    // No PECJ operator to clean up in stub mode
}
#endif

bool PECJComputeEngine::initialize(const ComputeConfig& config,
                                   TimeSeriesDB* db,
                                   core::ResourceHandle* resource_handle) {
    if (initialized_.load()) {
        return false; // Already initialized
    }
    
    if (!db) {
        return false; // Invalid arguments (db is required)
    }
    
    // resource_handle can be nullptr in stub mode
    
    // Store configuration and references
    config_ = config;
    db_ = db;
    resource_handle_ = resource_handle;
    
    // Validate configuration
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
        // Create PECJ config map - use direct make_shared to avoid macro issues
        auto pecj_config = std::make_shared<INTELLI::ConfigMap>();
        
        // Set window parameters (in microseconds)
        pecj_config->edit("windowLen", config_.window_len_us);
        pecj_config->edit("slideLen", config_.slide_len_us);
        pecj_config->edit("sLen", config_.s_buffer_len);
        pecj_config->edit("rLen", config_.r_buffer_len);
        pecj_config->edit("timeStep", config_.time_step_us);
        pecj_config->edit("joinSum", static_cast<uint64_t>(1)); // Enable join result counting
        
        // Set watermark parameters
        pecj_config->edit("wmTag", config_.watermark_tag);
        pecj_config->edit("watermarkTimeMs", config_.watermark_time_ms);
        pecj_config->edit("latenessMs", config_.lateness_ms);
        
        // Set operator-specific parameters
        if (config_.ima_disable_compensation) {
            pecj_config->edit("imaDisableCompensation", static_cast<uint64_t>(1));
        }
        if (config_.mswj_compensation) {
            pecj_config->edit("mswjCompensation", static_cast<uint64_t>(1));
        }
        
        // Sync operator_enum with operator_type string if needed
        PECJOperatorType op_type = stringToOperatorType(config_.operator_type);
        
        // Create operator based on type - support all PECJ operators
        switch (op_type) {
            case PECJOperatorType::IAWJ:
                pecj_operator_ = std::make_shared<OoOJoin::IAWJOperator>();
                break;
            case PECJOperatorType::MeanAQP:
                pecj_operator_ = std::make_shared<OoOJoin::MeanAQPIAWJOperator>();
                break;
            case PECJOperatorType::IMA:
                pecj_operator_ = std::make_shared<OoOJoin::IMAIAWJOperator>();
                break;
            case PECJOperatorType::MSWJ:
                pecj_operator_ = std::make_shared<OoOJoin::MSWJOperator>();
                break;
            case PECJOperatorType::AI:
                pecj_operator_ = std::make_shared<OoOJoin::AIOperator>();
                break;
            case PECJOperatorType::LinearSVI:
                pecj_operator_ = std::make_shared<OoOJoin::LinearSVIOperator>();
                break;
            case PECJOperatorType::IAWJSel:
                pecj_operator_ = std::make_shared<OoOJoin::IAWJSelOperator>();
                break;
            case PECJOperatorType::LazyIAWJSel:
                pecj_operator_ = std::make_shared<OoOJoin::LazyIAWJSelOperator>();
                break;
            case PECJOperatorType::SHJ:
                pecj_operator_ = std::make_shared<OoOJoin::RawSHJOperator>();
                break;
            case PECJOperatorType::PRJ:
                pecj_operator_ = std::make_shared<OoOJoin::RawPRJOperator>();
                break;
            case PECJOperatorType::PECJ:
                // PECJ uses IMAIAWJOperator internally (same as PECJOperator)
                pecj_operator_ = std::make_shared<OoOJoin::IMAIAWJOperator>();
                break;
            default:
                // Default to IAWJ
                pecj_operator_ = std::make_shared<OoOJoin::IAWJOperator>();
                break;
        }
        
        if (!pecj_operator_) {
            return false;
        }
        
        // Set configuration
        if (!pecj_operator_->setConfig(pecj_config)) {
            return false;
        }
        
        // Set window parameters
        pecj_operator_->setWindow(config_.window_len_us, config_.slide_len_us);
        
        // Synchronize time structure
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        pecj_operator_->syncTimeStruct(tv);
        
        // Start the operator
        if (!pecj_operator_->start()) {
            return false;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        // Log error
        std::cerr << "Failed to create PECJ operator: " << e.what() << std::endl;
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
    
#ifdef PECJ_FULL_INTEGRATION
    try {
        if (!pecj_operator_) {
            status.success = false;
            status.error = "PECJ operator not initialized";
            return status;
        }
        
        // Step 1: Query data from sageTSDB tables
        // Create TimeRange for query
        sage_tsdb::TimeRange query_range;
        query_range.start_time = time_range.start_us;
        query_range.end_time = time_range.end_us;
        
        auto s_data_tsdb = db_->query(config_.stream_s_table, query_range);
        auto r_data_tsdb = db_->query(config_.stream_r_table, query_range);
        
        status.input_s_count = s_data_tsdb.size();
        status.input_r_count = r_data_tsdb.size();
        
        // Debug: Print query range and results for first window
        if (window_id == 0) {
            std::cout << "    [DEBUG] Window range: [" << time_range.start_us 
                      << ", " << time_range.end_us << "] (length=" << (time_range.end_us - time_range.start_us) << " us)\n";
        }
        
        std::cout << "    [DEBUG] Queried S: " << s_data_tsdb.size() 
                  << " tuples, R: " << r_data_tsdb.size() << " tuples";
        
        // Debug: Check for matching keys in first window
        if (window_id == 0 && s_data_tsdb.size() > 0 && r_data_tsdb.size() > 0) {
            std::set<uint64_t> s_keys, r_keys;
            for (const auto& data : s_data_tsdb) {
                if (data.tags.count("key")) {
                    s_keys.insert(std::stoull(data.tags.at("key")));
                }
            }
            for (const auto& data : r_data_tsdb) {
                if (data.tags.count("key")) {
                    r_keys.insert(std::stoull(data.tags.at("key")));
                }
            }
            size_t common_keys = 0;
            for (const auto& key : s_keys) {
                if (r_keys.count(key)) common_keys++;
            }
            std::cout << " [S_unique=" << s_keys.size() << ", R_unique=" << r_keys.size() 
                      << ", Common=" << common_keys << "]";
        }
        std::cout << "\n";
        
        // Step 2: Feed data to PECJ operator
        size_t join_count_before = pecj_operator_->getResult();
        
        size_t s_fed = 0;
        for (const auto& data : s_data_tsdb) {
            // Convert TimeSeriesData to PECJ tuple
            OoOJoin::keyType key = 0;
            if (data.tags.count("key")) {
                key = std::stoull(data.tags.at("key"));
            }
            
            OoOJoin::valueType value = 0;
            if (data.fields.count("value")) {
                value = static_cast<OoOJoin::valueType>(std::stod(data.fields.at("value")));
            }
            
            // For this dataset, use arrivalTime as both eventTime and arrivalTime
            // (eventTime field in CSV has different scale/semantics)
            OoOJoin::tsType eventTime = data.timestamp;    // timestamp stores arrivalTime
            OoOJoin::tsType arrivalTime = data.timestamp;  // timestamp stores arrivalTime
            
            auto tuple = std::make_shared<OoOJoin::TrackTuple>(key, value, eventTime, arrivalTime);
            pecj_operator_->feedTupleS(tuple);
            s_fed++;
            
            // Debug: Print first few tuples with full details
            if (window_id == 0 && s_fed <= 5) {
                std::cout << "    [DEBUG] Fed S tuple: key=" << key 
                          << ", value=" << value 
                          << ", eventTime=" << eventTime
                          << ", arrivalTime=" << arrivalTime
                          << ", timestamp=" << data.timestamp << "\n";
            }
        }
        std::cout << "    [DEBUG] Fed " << s_fed << " S tuples\n";
        
        size_t r_fed = 0;
        for (const auto& data : r_data_tsdb) {
            // Convert TimeSeriesData to PECJ tuple
            OoOJoin::keyType key = 0;
            if (data.tags.count("key")) {
                key = std::stoull(data.tags.at("key"));
            }
            
            OoOJoin::valueType value = 0;
            if (data.fields.count("value")) {
                value = static_cast<OoOJoin::valueType>(std::stod(data.fields.at("value")));
            }
            
            // For this dataset, use arrivalTime as both eventTime and arrivalTime
            // (eventTime field in CSV has different scale/semantics)
            OoOJoin::tsType eventTime = data.timestamp;    // timestamp stores arrivalTime
            OoOJoin::tsType arrivalTime = data.timestamp;  // timestamp stores arrivalTime
            
            auto tuple = std::make_shared<OoOJoin::TrackTuple>(key, value, eventTime, arrivalTime);
            pecj_operator_->feedTupleR(tuple);
            r_fed++;
            
            // Debug: Print first few tuples with full details
            if (window_id == 0 && r_fed <= 5) {
                std::cout << "    [DEBUG] Fed R tuple: key=" << key 
                          << ", value=" << value 
                          << ", eventTime=" << eventTime
                          << ", arrivalTime=" << arrivalTime
                          << ", timestamp=" << data.timestamp << "\n";
            }
        }
        std::cout << "    [DEBUG] Fed " << r_fed << " R tuples\n";
        
        // Step 3: Get join results BEFORE stopping (stop() may clear state)
        size_t join_count_after = pecj_operator_->getResult();
        status.join_count = join_count_after - join_count_before;
        
        // Step 3.5: Get AQP result if operator supports it
        PECJOperatorType op_type = stringToOperatorType(config_.operator_type);
        if (operatorSupportsAQP(op_type)) {
            size_t aqp_result = pecj_operator_->getAQPResult();
            status.aqp_estimate = static_cast<double>(aqp_result);
            status.used_aqp = true;
            
            // Calculate AQP error
            if (status.join_count > 0) {
                status.aqp_error = std::abs(static_cast<double>(status.join_count) - status.aqp_estimate) 
                                 / static_cast<double>(status.join_count);
            }
            
            std::cout << "    [DEBUG] AQP result: " << aqp_result 
                      << ", error: " << (status.aqp_error * 100.0) << "%\n";
        }
        
        // Debug: Print join results
        std::cout << "    [DEBUG] Join results: before=" << join_count_before 
                  << ", after=" << join_count_after 
                  << ", delta=" << status.join_count << "\n";
        
        // Step 4: Stop operator after getting results
        pecj_operator_->stop();
        
        // Step 4: Calculate metrics
        auto end_time = std::chrono::steady_clock::now();
        status.computation_time_ms = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
        
        if (status.input_s_count > 0 && status.input_r_count > 0) {
            status.selectivity = static_cast<double>(status.join_count) /
                               (status.input_s_count * status.input_r_count);
        }
        
        status.success = true;
        
        // Step 5: Update global metrics
        updateMetrics(status);
        
    } catch (const std::exception& e) {
        status.success = false;
        status.error = std::string("Exception: ") + e.what();
    }
#else
    // Stub mode: return successful status without computation
    status.success = true;
    status.join_count = 0;
    status.computation_time_ms = 0.0;
    status.error = "PECJ not available (stub mode)";
#endif
    
    return status;
}

#ifdef PECJ_FULL_INTEGRATION
// Note: This function is not currently used - PECJ join is triggered via feedTuple* methods
ComputeStatus PECJComputeEngine::executeWithTimeout(
    const std::vector<OoOJoin::TrackTuple>& s_data,
    const std::vector<OoOJoin::TrackTuple>& r_data,
    uint64_t window_id,
    const TimeRange& time_range) {
    (void)time_range;  // Unused parameter
    (void)s_data;
    (void)r_data;
    
    ComputeStatus status;
    status.window_id = window_id;
    status.success = false;
    status.error = "executeWithTimeout not implemented - use feedTuple* methods instead";
    
    /* Legacy code - PECJ doesn't have a join() method
    try {
        auto start = std::chrono::steady_clock::now();
        
        // Execute PECJ join
        std::vector<std::pair<OoOJoin::TrackTuple, OoOJoin::TrackTuple>> join_results;
        
        // TODO: Implement timeout mechanism using resource_handle_->submitTask()
        // For now, execute directly
        if (pecj_operator_) {
            // PECJ processes tuples via feedTupleS/R, not a join() method
        }
        
    */ // End of legacy code
    
    return status;
}

#endif // PECJ_FULL_INTEGRATION (end of executeWithTimeout)

#ifdef PECJ_FULL_INTEGRATION
// Note: This function is not currently used
ComputeStatus PECJComputeEngine::fallbackToAQP(
    const std::vector<OoOJoin::TrackTuple>& s_data,
    const std::vector<OoOJoin::TrackTuple>& r_data,
    uint64_t window_id) {
    (void)s_data;
    (void)r_data;
    
    ComputeStatus status;
    status.window_id = window_id;
    status.success = false;
    status.error = "AQP fallback not implemented";
    /* Legacy code
    status.input_s_count = s_data.size();
    status.input_r_count = r_data.size();
    status.used_aqp = true;
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
        
        // PECJ doesn't have join() method
        //auto sample_results = pecj_operator_->join(s_sample, r_sample);
        
    */ // End of legacy code
    
    return status;
}
#endif // PECJ_FULL_INTEGRATION

#ifdef PECJ_FULL_INTEGRATION
std::vector<OoOJoin::TrackTuple> PECJComputeEngine::convertFromTable(
    const std::vector<std::vector<uint8_t>>& db_data) {
    
    std::vector<OoOJoin::TrackTuple> result;
    result.reserve(db_data.size());
    
    for (const auto& raw_data : db_data) {
        if (raw_data.size() < sizeof(OoOJoin::TrackTuple)) {
            continue; // Skip invalid data
        }
        
        // Deserialize from bytes
        OoOJoin::TrackTuple tuple(0); // Provide default key value
        std::memcpy(&tuple, raw_data.data(), sizeof(OoOJoin::TrackTuple));
        result.push_back(tuple);
    }
    
    return result;
}

std::vector<std::vector<uint8_t>> PECJComputeEngine::convertToTable(
    const std::vector<std::pair<OoOJoin::TrackTuple, OoOJoin::TrackTuple>>& pecj_result) {
    
    std::vector<std::vector<uint8_t>> result;
    result.reserve(pecj_result.size());
    
    for (const auto& [s_tuple, r_tuple] : pecj_result) {
        // Serialize join result to bytes
        // Format: [s_tuple][r_tuple]
        std::vector<uint8_t> data(sizeof(OoOJoin::TrackTuple) * 2);
        
        std::memcpy(data.data(), &s_tuple, sizeof(OoOJoin::TrackTuple));
        std::memcpy(data.data() + sizeof(OoOJoin::TrackTuple), 
                   &r_tuple, sizeof(OoOJoin::TrackTuple));
        
        result.push_back(std::move(data));
    }
    
    return result;
}
#endif // PECJ_FULL_INTEGRATION

bool PECJComputeEngine::writeResults(uint64_t window_id,
                                     const std::vector<std::vector<uint8_t>>& results,
                                     const ComputeStatus& status) {
    (void)window_id;  // Unused parameter
    (void)results;    // Unused parameter
    (void)status;     // Unused parameter
    
    try {
        // TODO: Implement using TableManager->getJoinResultTable()
        // Write each result tuple to the result table
        // for (const auto& result : results) {
        //     if (!db_->insert(config_.result_table, window_id, result)) {
        //         return false;
        //     }
        // }
        
        // Write metadata about the window
        // std::vector<uint8_t> metadata;
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
