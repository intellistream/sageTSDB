#pragma once

#include "../plugin_interface.h"
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

// Forward declarations for ML models
namespace TROCHPACK_VAE {
    class LinearVAE;
}

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Fault Detection Plugin using Machine Learning
 * 
 * This adapter provides real-time fault detection for industrial IoT
 * systems using variational autoencoders and statistical methods.
 * 
 * Features:
 * - Anomaly detection using VAE reconstruction error
 * - Time series pattern recognition
 * - Threshold-based alerting
 * - Adaptive learning
 * 
 * Design:
 * - Independent of sageTSDB internals
 * - Shares ML models with PECJ if needed
 * - Can be ported to other databases
 */
class FaultDetectionAdapter : public IAlgorithmPlugin {
public:
    explicit FaultDetectionAdapter(const PluginConfig& config);
    ~FaultDetectionAdapter() override;
    
    // IAlgorithmPlugin interface
    bool initialize(const PluginConfig& config) override;
    void feedData(const TimeSeriesData& data) override;
    AlgorithmResult process() override;
    std::map<std::string, int64_t> getStats() const override;
    void reset() override;
    bool start() override;
    bool stop() override;
    std::string getName() const override { return "FaultDetectionAdapter"; }
    std::string getVersion() const override { return "1.0.0"; }
    
    /**
     * @brief Detection method types
     */
    enum class DetectionMethod {
        ZSCORE,           // Statistical z-score
        VAE,              // Variational Autoencoder
        HYBRID            // Combination of methods
    };
    
    /**
     * @brief Anomaly severity levels
     */
    enum class Severity {
        NORMAL = 0,
        WARNING = 1,
        CRITICAL = 2
    };
    
    /**
     * @brief Detection result structure
     */
    struct DetectionResult {
        int64_t timestamp;
        bool is_anomaly;
        double anomaly_score;
        Severity severity;
        std::string description;
        std::map<std::string, double> features;
    };
    
    /**
     * @brief Get latest detection results
     */
    std::vector<DetectionResult> getDetectionResults(size_t count = 10) const;
    
    /**
     * @brief Update model with new training data
     */
    void updateModel(const std::vector<TimeSeriesData>& training_data);
    
    /**
     * @brief Set detection threshold
     */
    void setThreshold(double threshold);
    
    /**
     * @brief Get current model accuracy metrics
     */
    std::map<std::string, double> getModelMetrics() const;

private:
    /**
     * @brief Detect anomalies using statistical method
     */
    DetectionResult detectZScore(const TimeSeriesData& data);
    
    /**
     * @brief Detect anomalies using VAE
     */
    DetectionResult detectVAE(const TimeSeriesData& data);
    
    /**
     * @brief Compute reconstruction error
     */
    double computeReconstructionError(const std::vector<double>& input,
                                     const std::vector<double>& reconstructed);
    
    /**
     * @brief Update statistics for z-score calculation
     */
    void updateStatistics(double value);
    
    /**
     * @brief Initialize ML model
     */
    bool initializeModel();
    
    // Configuration
    PluginConfig config_;
    DetectionMethod detection_method_;
    double threshold_;
    size_t window_size_;
    
    // ML Model (shared with PECJ if configured)
    std::shared_ptr<TROCHPACK_VAE::LinearVAE> vae_model_;
    
    // Statistical tracking
    std::deque<double> value_history_;
    double running_mean_;
    double running_variance_;
    size_t sample_count_;
    
    // Results history
    mutable std::mutex results_mutex_;
    std::deque<DetectionResult> detection_history_;
    size_t max_history_size_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    size_t total_samples_;
    size_t anomalies_detected_;
    int64_t total_detection_time_us_;
    
    // State
    bool initialized_;
    bool running_;
    std::mutex state_mutex_;
};

} // namespace plugins
} // namespace sage_tsdb
