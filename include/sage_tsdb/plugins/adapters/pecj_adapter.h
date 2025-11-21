#pragma once

#include "../plugin_interface.h"
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations to avoid including PECJ headers directly
// This maintains decoupling
namespace OoOJoin {
    class AbstractOperator;
    class TrackTuple;
}

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
 */
class PECJAdapter : public IAlgorithmPlugin {
public:
    explicit PECJAdapter(const PluginConfig& config);
    ~PECJAdapter() override;
    
    // IAlgorithmPlugin interface
    bool initialize(const PluginConfig& config) override;
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
     */
    void feedStreamS(const TimeSeriesData& data);
    
    /**
     * @brief Feed data for right stream (R stream)
     */
    void feedStreamR(const TimeSeriesData& data);
    
    /**
     * @brief Get join result count
     */
    size_t getJoinResult() const;
    
    /**
     * @brief Get approximate join result
     */
    double getApproximateResult() const;

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
    
    // PECJ operator instance (pimpl idiom for decoupling)
    std::shared_ptr<OoOJoin::AbstractOperator> pecj_operator_;
    
    // Configuration
    PluginConfig config_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    size_t tuples_processed_s_;
    size_t tuples_processed_r_;
    size_t join_results_;
    int64_t total_latency_us_;
    
    // State
    bool initialized_;
    bool running_;
    std::mutex state_mutex_;
};

} // namespace plugins
} // namespace sage_tsdb
