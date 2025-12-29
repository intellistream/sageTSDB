/**
 * @file pecj_compute_engine.h
 * @brief PECJ Compute Engine for Deep Integration Mode
 * 
 * This file implements the PECJ compute engine as a stateless computation component
 * in the deep integration architecture. The engine does not hold any data buffers,
 * does not create threads, and does not manage its own lifecycle.
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-04
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <shared_mutex>
#include <atomic>

#ifdef PECJ_MODE_INTEGRATED

// Forward declarations
namespace sage_tsdb {
class TimeSeriesDB;  // TimeSeriesDB is in sage_tsdb namespace, not sage_tsdb::core

namespace core {
class ResourceHandle;
}
}

namespace OoOJoin {
class AbstractOperator;
struct TrackTuple;
}

namespace sage_tsdb {
namespace compute {

/**
 * @brief Time range specification for window queries
 */
struct TimeRange {
    int64_t start_us;  ///< Start timestamp in microseconds
    int64_t end_us;    ///< End timestamp in microseconds
    
    TimeRange() : start_us(0), end_us(0) {}
    TimeRange(int64_t start, int64_t end) : start_us(start), end_us(end) {}
    
    inline int64_t duration() const { return end_us - start_us; }
    inline bool contains(int64_t ts) const { return ts >= start_us && ts < end_us; }
    inline bool valid() const { return end_us > start_us; }
};

/**
 * @brief PECJ operator type enumeration
 * 
 * Defines all available operator types from the PECJ library:
 * - IAWJ: Intra-window join operator, only considers a single window
 * - MeanAQP: AQP strategy using exponential weighted moving average for prediction
 * - IMA: Incremental Moving Average IAWJ with AQP support (EAGER join)
 * - MSWJ: Multi-stream window join (ICDE2016)
 * - AI: AI-based operator
 * - LinearSVI: Linear Stochastic Variational Inference operator
 * - IAWJSel: IAWJ with selectivity-based AQP strategy (coarse-grained)
 * - LazyIAWJSel: Lazy evaluation PECJ join with selectivity
 * - SHJ: Symmetric Hash Join (raw baseline)
 * - PRJ: Progressive Join (raw baseline)
 * - PECJ: PECJ operator with full compensation
 */
enum class PECJOperatorType {
    IAWJ,           ///< Intra-window join - single window only
    MeanAQP,        ///< Mean-based AQP prediction
    IMA,            ///< Incremental Moving Average IAWJ (EAGER join)
    MSWJ,           ///< Multi-stream Window Join (ICDE2016)
    AI,             ///< AI-based operator
    LinearSVI,      ///< Linear Stochastic Variational Inference
    IAWJSel,        ///< IAWJ with selectivity-based AQP
    LazyIAWJSel,    ///< Lazy evaluation PECJ join
    SHJ,            ///< Symmetric Hash Join (baseline)
    PRJ,            ///< Progressive Join (baseline)
    PECJ            ///< Full PECJ operator
};

/**
 * @brief Convert operator type enum to string tag
 * @param type The operator type enum
 * @return String tag used by PECJ OperatorTable
 */
inline std::string operatorTypeToString(PECJOperatorType type) {
    switch (type) {
        case PECJOperatorType::IAWJ:        return "IAWJ";
        case PECJOperatorType::MeanAQP:     return "MeanAQP";
        case PECJOperatorType::IMA:         return "IMA";
        case PECJOperatorType::MSWJ:        return "MSWJ";
        case PECJOperatorType::AI:          return "AI";
        case PECJOperatorType::LinearSVI:   return "LinearSVI";
        case PECJOperatorType::IAWJSel:     return "IAWJSel";
        case PECJOperatorType::LazyIAWJSel: return "LazyIAWJSel";
        case PECJOperatorType::SHJ:         return "SHJ";
        case PECJOperatorType::PRJ:         return "PRJ";
        case PECJOperatorType::PECJ:        return "IMA";  // PECJ uses IMA internally
        default:                            return "IAWJ";
    }
}

/**
 * @brief Convert string tag to operator type enum
 * @param tag The string tag
 * @return Corresponding operator type enum
 */
inline PECJOperatorType stringToOperatorType(const std::string& tag) {
    if (tag == "IAWJ")        return PECJOperatorType::IAWJ;
    if (tag == "MeanAQP")     return PECJOperatorType::MeanAQP;
    if (tag == "IMA")         return PECJOperatorType::IMA;
    if (tag == "MSWJ")        return PECJOperatorType::MSWJ;
    if (tag == "AI")          return PECJOperatorType::AI;
    if (tag == "LinearSVI")   return PECJOperatorType::LinearSVI;
    if (tag == "IAWJSel")     return PECJOperatorType::IAWJSel;
    if (tag == "LazyIAWJSel") return PECJOperatorType::LazyIAWJSel;
    if (tag == "SHJ")         return PECJOperatorType::SHJ;
    if (tag == "PRJ")         return PECJOperatorType::PRJ;
    if (tag == "PECJ" || tag == "PEC") return PECJOperatorType::PECJ;
    return PECJOperatorType::IAWJ;  // Default
}

/**
 * @brief Check if operator supports AQP (Approximate Query Processing)
 * @param type The operator type
 * @return true if operator supports getAQPResult()
 */
inline bool operatorSupportsAQP(PECJOperatorType type) {
    switch (type) {
        case PECJOperatorType::MeanAQP:
        case PECJOperatorType::IMA:
        case PECJOperatorType::MSWJ:
        case PECJOperatorType::IAWJSel:
        case PECJOperatorType::LazyIAWJSel:
        case PECJOperatorType::PECJ:
            return true;
        default:
            return false;
    }
}

/**
 * @brief PECJ algorithm configuration
 */
struct ComputeConfig {
    // Window parameters
    uint64_t window_len_us = 1000000;     ///< Window length (1s default)
    uint64_t slide_len_us = 500000;       ///< Slide length (500ms default)
    
    // Algorithm parameters
    std::string operator_type = "IAWJ";   ///< Operator type string tag
    PECJOperatorType operator_enum = PECJOperatorType::IAWJ; ///< Operator type enum
    uint64_t max_delay_us = 100000;       ///< Maximum allowed delay (100ms)
    double aqp_threshold = 0.05;          ///< AQP error threshold (5%)
    
    // PECJ-specific parameters
    uint64_t s_buffer_len = 100000;       ///< S buffer size
    uint64_t r_buffer_len = 100000;       ///< R buffer size
    uint64_t time_step_us = 1000;         ///< Simulation time step in us
    std::string watermark_tag = "arrival"; ///< Watermark generator tag (arrival, lateness)
    uint64_t watermark_time_ms = 100;     ///< Watermark time for ArrivalWM (ms)
    uint64_t lateness_ms = 50;            ///< Max allowed lateness for LatenessWM (ms)
    
    // Join result mode
    bool join_sum = false;                 ///< false=Join Count (count matching pairs), true=Join Sum (count*avg aggregation)
    
    // IMA/PECJ specific
    bool ima_disable_compensation = false; ///< Disable compensation in IMA (simple eager join)
    
    // MSWJ specific
    bool mswj_compensation = false;        ///< Enable linear compensation in MSWJ
    
    // Resource limits
    size_t max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  ///< 2GB default
    int max_threads = 4;                  ///< Thread limit
    
    // Performance tuning
    bool enable_aqp = true;               ///< Enable AQP fallback
    bool enable_simd = true;              ///< Enable SIMD optimization
    uint64_t timeout_ms = 1000;           ///< Computation timeout
    
    // Table names
    std::string stream_s_table = "stream_s";
    std::string stream_r_table = "stream_r";
    std::string result_table = "join_results";
};

/**
 * @brief Computation status after executing a window join
 */
struct ComputeStatus {
    bool success = false;                 ///< Whether computation succeeded
    std::string error;                    ///< Error message if failed
    
    // Result statistics
    uint64_t window_id = 0;               ///< Window identifier
    size_t join_count = 0;                ///< Number of exact join results
    double aqp_estimate = 0.0;            ///< AQP estimated count (if enabled)
    
    // Performance metrics
    double computation_time_ms = 0.0;     ///< Computation duration
    size_t input_s_count = 0;             ///< Stream S input tuples
    size_t input_r_count = 0;             ///< Stream R input tuples
    size_t memory_used_bytes = 0;         ///< Memory consumed
    
    // Quality metrics
    double selectivity = 0.0;             ///< join_count / (|S| * |R|)
    double aqp_error = 0.0;               ///< |exact - aqp| / exact
    bool used_aqp = false;                ///< Whether AQP was used
    bool timeout_occurred = false;        ///< Whether timeout happened
};

/**
 * @brief Runtime metrics for monitoring
 */
struct ComputeMetrics {
    // Throughput metrics
    uint64_t total_windows_completed = 0;
    uint64_t total_tuples_processed = 0;
    double avg_throughput_events_per_sec = 0.0;
    
    // Latency metrics (milliseconds)
    double avg_window_latency_ms = 0.0;
    double min_window_latency_ms = 0.0;
    double max_window_latency_ms = 0.0;
    double p99_window_latency_ms = 0.0;
    
    // Resource metrics
    size_t peak_memory_bytes = 0;
    size_t avg_memory_bytes = 0;
    int active_threads = 0;
    
    // Quality metrics
    double avg_join_selectivity = 0.0;
    double avg_aqp_error_rate = 0.0;
    uint64_t aqp_invocations = 0;
    
    // Error metrics
    uint64_t failed_windows = 0;
    uint64_t timeout_windows = 0;
    uint64_t retry_count = 0;
};

/**
 * @brief PECJ Compute Engine (Stateless)
 * 
 * Design Principles:
 * - Does NOT hold any data buffers
 * - Does NOT create threads (uses ResourceHandle to submit tasks)
 * - Does NOT manage lifecycle (scheduled by sageTSDB)
 * - Computation results are written back to sageTSDB tables
 * 
 * Usage Pattern:
 * 1. Initialize once with configuration
 * 2. Execute window joins via executeWindowJoin() (called by WindowScheduler)
 * 3. Query metrics via getMetrics()
 * 4. Reset state via reset() if needed
 */
class PECJComputeEngine {
public:
    PECJComputeEngine();
    // Destructor declared here, implemented in .cpp to handle conditional compilation
    ~PECJComputeEngine();
    
    // Disable copy and move
    PECJComputeEngine(const PECJComputeEngine&) = delete;
    PECJComputeEngine& operator=(const PECJComputeEngine&) = delete;
    PECJComputeEngine(PECJComputeEngine&&) = delete;
    PECJComputeEngine& operator=(PECJComputeEngine&&) = delete;
    
    /**
     * @brief Initialize the compute engine (one-time configuration)
     * @param config Algorithm configuration
     * @param db Pointer to TimeSeriesDB (not owned)
     * @param resource_handle Resource handle for task submission (not owned)
     * @return true if initialization succeeded
     */
    bool initialize(const ComputeConfig& config,
                   TimeSeriesDB* db,
                   core::ResourceHandle* resource_handle);
    
    /**
     * @brief Execute window join computation (synchronous call)
     * @param window_id Window identifier
     * @param time_range Time range [start, end)
     * @return Computation status
     * 
     * Execution Flow:
     * 1. Query stream_s and stream_r from db for the time_range
     * 2. Convert data to PECJ format
     * 3. Invoke PECJ core algorithm to perform join
     * 4. Convert results back to sageTSDB format
     * 5. Write results to join_results table
     * 6. Return computation statistics
     */
    ComputeStatus executeWindowJoin(uint64_t window_id,
                                    const TimeRange& time_range);
    
    /**
     * @brief Get runtime metrics (thread-safe)
     * @return Current metrics snapshot
     */
    ComputeMetrics getMetrics() const;
    
    /**
     * @brief Reset computation state
     * 
     * This clears cached data, resets counters, and prepares for fresh computation.
     * Note: This does NOT clear data in sageTSDB tables.
     */
    void reset();
    
    /**
     * @brief Check if the engine is initialized
     */
    bool isInitialized() const { return initialized_.load(); }
    
    /**
     * @brief Get current configuration
     */
    const ComputeConfig& getConfig() const { return config_; }

private:
    // === Core Components ===
    TimeSeriesDB* db_;                          ///< Database reference (not owned)
    core::ResourceHandle* resource_handle_;     ///< Resource handle (not owned)
    ComputeConfig config_;                      ///< Algorithm configuration
    std::atomic<bool> initialized_;             ///< Initialization flag
    
    // === PECJ Operator ===
#ifdef PECJ_FULL_INTEGRATION
    std::shared_ptr<OoOJoin::AbstractOperator> pecj_operator_;  // PECJ uses shared_ptr
#endif
    
    // === Metrics Tracking ===
    mutable std::shared_mutex metrics_mutex_;   ///< Protects metrics_
    ComputeMetrics metrics_;                    ///< Runtime metrics
    std::vector<double> latency_samples_;       ///< Latency history for percentile calculation
    
    // === Memory Management ===
    std::atomic<size_t> current_memory_usage_; ///< Current memory usage
    
    // === Private Methods ===
    
#ifdef PECJ_FULL_INTEGRATION
    /**
     * @brief Convert sageTSDB data to PECJ TrackTuple format
     */
    std::vector<OoOJoin::TrackTuple> convertFromTable(
        const std::vector<std::vector<uint8_t>>& db_data);
    
    /**
     * @brief Convert PECJ join result to sageTSDB format
     */
    std::vector<std::vector<uint8_t>> convertToTable(
        const std::vector<std::pair<OoOJoin::TrackTuple, OoOJoin::TrackTuple>>& pecj_result);
#endif
    
    /**
     * @brief Update metrics after window completion
     */
    void updateMetrics(const ComputeStatus& status);
    
    /**
     * @brief Check if memory usage exceeds limit
     */
    bool checkMemoryLimit() const;
    
    /**
     * @brief Create PECJ operator based on configuration
     */
    bool createPECJOperator();
    
#ifdef PECJ_FULL_INTEGRATION
    /**
     * @brief Execute join with timeout protection
     */
    ComputeStatus executeWithTimeout(
        const std::vector<OoOJoin::TrackTuple>& s_data,
        const std::vector<OoOJoin::TrackTuple>& r_data,
        uint64_t window_id,
        const TimeRange& time_range);
    
    /**
     * @brief Fallback to AQP estimation
     */
    ComputeStatus fallbackToAQP(
        const std::vector<OoOJoin::TrackTuple>& s_data,
        const std::vector<OoOJoin::TrackTuple>& r_data,
        uint64_t window_id);
#endif
    
    /**
     * @brief Write join results to database table
     */
    bool writeResults(uint64_t window_id,
                     const std::vector<std::vector<uint8_t>>& results,
                     const ComputeStatus& status);
};

} // namespace compute
} // namespace sage_tsdb

#endif // PECJ_MODE_INTEGRATED
