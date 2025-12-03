#pragma once

#include "../plugin_interface.h"
#include "../event_bus.h"
#include "../resource_manager.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Conditional inclusion based on PECJ availability
#ifdef PECJ_FULL_INTEGRATION
// Only include necessary PECJ headers to avoid torch dependency
#include <Common/Tuples.h>
#include <Common/Window.h>
#include <Operator/AbstractOperator.h>
#include <Operator/OperatorTable.h>
#include <Utils/ConfigMap.hpp>
#else
// Forward declarations to avoid including PECJ headers directly
// This maintains decoupling when PECJ is not available
namespace OoOJoin {
    class AbstractOperator;
    class TrackTuple;
}
namespace INTELLI {
    class ConfigMap;
}
#endif

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Adapter for PECJ (Predictive Error-bounded Computation for Joins)
 * 
 * This adapter integrates the PECJ algorithm into sageTSDB without
 * creating dependencies between PECJ and sageTSDB core.
 * 
 * PECJ Features:
 * - Out-of-order stream join
 * - Variational inference for prediction
 * - Watermark-based windowing
 * - Error-bounded approximate query processing
 * 
 * Design:
 * - Wraps PECJ operator as a plugin
 * - Converts TimeSeriesData to PECJ TrackTuple
 * - Manages PECJ lifecycle independently
 * - Can be ported to other databases by changing only this adapter
 * 
 * Multi-threading Model:
 * - Data ingestion is lock-free using SPSC queues
 * - PECJ internal threads handle join computation
 * - Window results are published via EventBus
 * 
 * Window Visibility:
 * - S and R streams share the same window state inside PECJ
 * - Window triggers are broadcast via WINDOW_TRIGGERED events
 * - Zero-copy data sharing via shared_ptr payloads
 */
class PECJAdapter : public IAlgorithmPlugin {
public:
    /**
     * @brief PECJ operator types supported
     */
    enum class OperatorType {
        IAWJ,           // Interval-Aware Window Join
        IMA,            // IMA-based AQP
        MSWJ,           // Multi-Stream Window Join
        AI,             // AI-enhanced operator
        LINEAR_SVI,     // Linear Stochastic Variational Inference
        MEAN_AQP,       // Mean-based AQP
        SHJ,            // Symmetric Hash Join
        PRJ             // Partitioned Range Join
    };
    
    /**
     * @brief Join window configuration
     */
    struct WindowConfig {
        uint64_t window_len_us = 1000000;    // Window length in microseconds
        uint64_t slide_len_us = 500000;      // Slide length in microseconds
        uint64_t lateness_ms = 100;          // Max allowed lateness in ms
        uint64_t time_step_us = 1000;        // Internal time step
        size_t s_buffer_len = 10000;         // S stream buffer size
        size_t r_buffer_len = 10000;         // R stream buffer size
    };
    
    explicit PECJAdapter(const PluginConfig& config);
    ~PECJAdapter() override;
    
    // IAlgorithmPlugin interface
    bool initialize(const PluginConfig& config) override;
    
    /**
     * @brief Initialize with resource management (NEW API)
     * @param config Plugin configuration
     * @param resource_request Resource requirements
     * @param resource_handle Allocated resource handle from ResourceManager
     * @return true if initialization succeeds
     * 
     * In Integrated mode, tasks are submitted via resource_handle instead of
     * creating independent threads. In Stub mode, resource_handle may be nullptr.
     */
    bool initialize(const PluginConfig& config, 
                   const ResourceRequest& resource_request,
                   std::shared_ptr<ResourceHandle> resource_handle);
    
    void feedData(const TimeSeriesData& data) override;
    AlgorithmResult process() override;
    std::map<std::string, int64_t> getStats() const override;
    void reset() override;
    bool start() override;
    bool stop() override;
    std::string getName() const override { return "PECJAdapter"; }
    std::string getVersion() const override { return "1.0.0"; }
    
    /**
     * @brief Feed data for left stream (S stream)
     * @param data Time series data point
     * @note Thread-safe, uses lock-free queue internally
     */
    void feedStreamS(const TimeSeriesData& data);
    
    /**
     * @brief Feed data for right stream (R stream)
     * @param data Time series data point
     * @note Thread-safe, uses lock-free queue internally
     */
    void feedStreamR(const TimeSeriesData& data);
    
    /**
     * @brief Get exact join result count
     */
    size_t getJoinResult() const;
    
    /**
     * @brief Get approximate join result (AQP estimation)
     */
    double getApproximateResult() const;
    
    /**
     * @brief Get time breakdown statistics from PECJ
     */
    std::map<std::string, int64_t> getTimeBreakdown() const;
    
    /**
     * @brief Set event bus for publishing results
     */
    void setEventBus(EventBus* bus) { event_bus_ = bus; }
    
    /**
     * @brief Set operator type
     */
    void setOperatorType(OperatorType type) { operator_type_ = type; }
    
    /**
     * @brief Get current window configuration
     */
    const WindowConfig& getWindowConfig() const { return window_config_; }
    
    /**
     * @brief Get current resource usage (for monitoring)
     */
    ResourceUsage getResourceUsage() const;

private:
    /**
     * @brief Convert TimeSeriesData to PECJ TrackTuple
     */
    std::shared_ptr<OoOJoin::TrackTuple> convertToTrackTuple(
        const TimeSeriesData& data, bool is_s_stream);
    
    /**
     * @brief Initialize PECJ operator with config
     */
    bool initializePECJ();
    
    /**
     * @brief Parse configuration parameters
     */
    void parseConfig(const PluginConfig& config);
    
    /**
     * @brief Worker thread for processing data
     */
    void workerLoop();
    
    /**
     * @brief Publish window result via EventBus
     */
    void publishWindowResult(size_t join_count, double aqp_result);
    
    // PECJ operator instance
    std::shared_ptr<OoOJoin::AbstractOperator> pecj_operator_;
    
#ifdef PECJ_FULL_INTEGRATION
    // PECJ configuration map
    std::shared_ptr<INTELLI::ConfigMap> pecj_config_;
    
    // Time base for PECJ
    struct timeval time_base_;
#endif
    
    // Operator type
    OperatorType operator_type_ = OperatorType::IMA;
    
    // Window configuration
    WindowConfig window_config_;
    
    // Plugin configuration
    PluginConfig config_;
    
    // Event bus for publishing results
    EventBus* event_bus_ = nullptr;
    
    // Statistics (thread-safe)
    mutable std::mutex stats_mutex_;
    std::atomic<size_t> tuples_processed_s_{0};
    std::atomic<size_t> tuples_processed_r_{0};
    std::atomic<size_t> join_results_{0};
    std::atomic<int64_t> total_latency_us_{0};
    
    // Worker thread (Baseline mode only)
    std::thread worker_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // Data queues (for async processing)
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::pair<TimeSeriesData, bool>> data_queue_; // (data, is_s_stream)
    
    // State mutex
    std::mutex state_mutex_;
    
    // Resource management (Integrated mode)
    ResourceRequest resource_request_;
    std::shared_ptr<ResourceHandle> resource_handle_;
    std::atomic<uint64_t> queue_length_{0};
    
    // Mode detection
    enum class RunMode {
        Stub,        // No PECJ, stub behavior
        Baseline,    // Independent threads (legacy)
        Integrated   // ResourceManager-controlled
    };
    RunMode run_mode_ = RunMode::Stub;
};

} // namespace plugins
} // namespace sage_tsdb
