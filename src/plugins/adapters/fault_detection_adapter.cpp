#include "sage_tsdb/plugins/adapters/fault_detection_adapter.h"
#include "sage_tsdb/plugins/plugin_registry.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <algorithm>

// Stub: Include PECJ's VAE model if using shared models
// #include "Common/LinearVAE.h"

namespace sage_tsdb {
namespace plugins {

FaultDetectionAdapter::FaultDetectionAdapter(const PluginConfig& config)
    : config_(config),
      detection_method_(DetectionMethod::HYBRID),
      threshold_(2.5),
      window_size_(100),
      running_mean_(0.0),
      running_variance_(0.0),
      sample_count_(0),
      max_history_size_(1000),
      total_samples_(0),
      anomalies_detected_(0),
      total_detection_time_us_(0),
      initialized_(false),
      running_(false) {
}

FaultDetectionAdapter::~FaultDetectionAdapter() {
    stop();
}

bool FaultDetectionAdapter::initialize(const PluginConfig& config) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (initialized_) {
        return true;
    }
    
    config_ = config;
    
    // Parse detection method
    if (config_.find("method") != config_.end()) {
        std::string method = config_.at("method");
        if (method == "zscore") {
            detection_method_ = DetectionMethod::ZSCORE;
        } else if (method == "vae") {
            detection_method_ = DetectionMethod::VAE;
        } else if (method == "hybrid") {
            detection_method_ = DetectionMethod::HYBRID;
        }
    }
    
    // Parse threshold
    if (config_.find("threshold") != config_.end()) {
        threshold_ = std::stod(config_.at("threshold"));
    }
    
    // Parse window size
    if (config_.find("window_size") != config_.end()) {
        window_size_ = std::stoull(config_.at("window_size"));
    }
    
    // Parse max history size
    if (config_.find("max_history") != config_.end()) {
        max_history_size_ = std::stoull(config_.at("max_history"));
    }
    
    // Initialize ML model if needed
    if (detection_method_ == DetectionMethod::VAE || 
        detection_method_ == DetectionMethod::HYBRID) {
        if (!initializeModel()) {
            std::cerr << "Warning: Failed to initialize VAE model, falling back to z-score" << std::endl;
            detection_method_ = DetectionMethod::ZSCORE;
        }
    }
    
    initialized_ = true;
    return true;
}

bool FaultDetectionAdapter::initializeModel() {
    try {
        // Stub: Initialize Linear VAE model
        // vae_model_ = std::make_shared<TROCHPACK_VAE::LinearVAE>();
        
        // In stub mode, just print message
        std::cout << "VAE model initialized (stub mode)" << std::endl;
        
        // Load pre-trained model if path is provided
        if (config_.find("model_path") != config_.end()) {
            std::string model_path = config_.at("model_path");
            std::cout << "Would load VAE model from: " << model_path << std::endl;
        }
        
        return true;  // Stub: Allow initialization to succeed
    } catch (const std::exception& e) {
        std::cerr << "Exception in initializeModel: " << e.what() << std::endl;
        return false;
    }
}

void FaultDetectionAdapter::feedData(const TimeSeriesData& data) {
    if (!initialized_ || !running_) {
        return;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    DetectionResult result;
    
    switch (detection_method_) {
        case DetectionMethod::ZSCORE:
            result = detectZScore(data);
            break;
        case DetectionMethod::VAE:
            result = detectVAE(data);
            break;
        case DetectionMethod::HYBRID:
            {
                auto zscore_result = detectZScore(data);
                auto vae_result = detectVAE(data);
                // Combine results: anomaly if either method detects it
                result = zscore_result;
                result.is_anomaly = zscore_result.is_anomaly || vae_result.is_anomaly;
                result.anomaly_score = std::max(zscore_result.anomaly_score, 
                                                vae_result.anomaly_score);
                result.features["zscore"] = zscore_result.anomaly_score;
                result.features["vae_error"] = vae_result.anomaly_score;
            }
            break;
    }
    
    // Store result
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        detection_history_.push_back(result);
        if (detection_history_.size() > max_history_size_) {
            detection_history_.pop_front();
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    // Update statistics
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_samples_++;
        if (result.is_anomaly) {
            anomalies_detected_++;
        }
        total_detection_time_us_ += latency;
    }
}

FaultDetectionAdapter::DetectionResult FaultDetectionAdapter::detectZScore(
    const TimeSeriesData& data) {
    
    DetectionResult result;
    result.timestamp = data.timestamp;
    result.is_anomaly = false;
    result.anomaly_score = 0.0;
    result.severity = Severity::NORMAL;
    
    // Extract scalar value
    double value = data.as_double();
    
    // Update statistics
    updateStatistics(value);
    
    // Need enough samples for reliable detection
    if (sample_count_ < 10) {
        result.description = "Insufficient data for detection";
        return result;
    }
    
    // Calculate z-score
    double std_dev = std::sqrt(running_variance_);
    if (std_dev < 1e-9) {
        result.description = "No variation in data";
        return result;
    }
    
    double zscore = std::abs(value - running_mean_) / std_dev;
    result.anomaly_score = zscore;
    result.features["mean"] = running_mean_;
    result.features["std_dev"] = std_dev;
    result.features["zscore"] = zscore;
    
    // Determine if anomaly
    if (zscore > threshold_) {
        result.is_anomaly = true;
        
        if (zscore > threshold_ * 2.0) {
            result.severity = Severity::CRITICAL;
            result.description = "Critical anomaly detected (z-score: " + 
                               std::to_string(zscore) + ")";
        } else {
            result.severity = Severity::WARNING;
            result.description = "Anomaly detected (z-score: " + 
                               std::to_string(zscore) + ")";
        }
    } else {
        result.description = "Normal operation";
    }
    
    return result;
}

FaultDetectionAdapter::DetectionResult FaultDetectionAdapter::detectVAE(
    const TimeSeriesData& data) {
    
    DetectionResult result;
    result.timestamp = data.timestamp;
    result.is_anomaly = false;
    result.anomaly_score = 0.0;
    result.severity = Severity::NORMAL;
    
    // Stub: VAE model not available in stub mode
    // In full implementation, use VAE for reconstruction and error calculation
    
    try {
        // Stub: Simulate VAE reconstruction error
        // In full implementation:
        // - vae_model_->reconstruct(input)
        // - computeReconstructionError(input, reconstructed)
        
        // For stub mode, use simple heuristic: larger deviations = higher error
        double zscore_result = detectZScore(data).anomaly_score;
        double error = std::abs(zscore_result);
        
        result.anomaly_score = error;
        result.features["reconstruction_error"] = error;
        
        // Determine if anomaly based on error threshold
        double error_threshold = threshold_ * 0.1;  // Adjust based on model
        if (error > error_threshold) {
            result.is_anomaly = true;
            
            if (error > error_threshold * 2.0) {
                result.severity = Severity::CRITICAL;
                result.description = "Critical anomaly detected by VAE (error: " + 
                                   std::to_string(error) + ")";
            } else {
                result.severity = Severity::WARNING;
                result.description = "Anomaly detected by VAE (error: " + 
                                   std::to_string(error) + ")";
            }
        } else {
            result.description = "Normal operation (VAE)";
        }
        
    } catch (const std::exception& e) {
        result.description = std::string("VAE detection error: ") + e.what();
    }
    
    return result;
}

double FaultDetectionAdapter::computeReconstructionError(
    const std::vector<double>& input,
    const std::vector<double>& reconstructed) {
    
    if (input.size() != reconstructed.size()) {
        return 0.0;
    }
    
    // Calculate MSE
    double sum_squared_error = 0.0;
    for (size_t i = 0; i < input.size(); i++) {
        double diff = input[i] - reconstructed[i];
        sum_squared_error += diff * diff;
    }
    
    return std::sqrt(sum_squared_error / input.size());  // RMSE
}

void FaultDetectionAdapter::updateStatistics(double value) {
    value_history_.push_back(value);
    if (value_history_.size() > window_size_) {
        value_history_.pop_front();
    }
    
    // Update running statistics using Welford's online algorithm
    sample_count_++;
    double delta = value - running_mean_;
    running_mean_ += delta / sample_count_;
    double delta2 = value - running_mean_;
    running_variance_ += (delta * delta2 - running_variance_) / sample_count_;
}

AlgorithmResult FaultDetectionAdapter::process() {
    if (!initialized_ || !running_) {
        return AlgorithmResult();
    }
    
    AlgorithmResult result;
    result.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    result.metrics["total_samples"] = static_cast<double>(total_samples_);
    result.metrics["anomalies_detected"] = static_cast<double>(anomalies_detected_);
    result.metrics["anomaly_rate"] = total_samples_ > 0 ? 
        static_cast<double>(anomalies_detected_) / total_samples_ : 0.0;
    
    return result;
}

std::map<std::string, int64_t> FaultDetectionAdapter::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    return {
        {"total_samples", static_cast<int64_t>(total_samples_)},
        {"anomalies_detected", static_cast<int64_t>(anomalies_detected_)},
        {"avg_detection_time_us", total_samples_ > 0 ? 
            total_detection_time_us_ / total_samples_ : 0}
    };
}

void FaultDetectionAdapter::reset() {
    std::lock_guard<std::mutex> lock1(stats_mutex_);
    std::lock_guard<std::mutex> lock2(results_mutex_);
    
    total_samples_ = 0;
    anomalies_detected_ = 0;
    total_detection_time_us_ = 0;
    
    value_history_.clear();
    running_mean_ = 0.0;
    running_variance_ = 0.0;
    sample_count_ = 0;
    
    detection_history_.clear();
}

bool FaultDetectionAdapter::start() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!initialized_) {
        return false;
    }
    
    if (running_) {
        return true;
    }
    
    running_ = true;
    return true;
}

bool FaultDetectionAdapter::stop() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (!running_) {
        return true;
    }
    
    running_ = false;
    return true;
}

std::vector<FaultDetectionAdapter::DetectionResult> 
FaultDetectionAdapter::getDetectionResults(size_t count) const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    
    std::vector<DetectionResult> results;
    size_t start = detection_history_.size() > count ? 
                   detection_history_.size() - count : 0;
    
    for (size_t i = start; i < detection_history_.size(); i++) {
        results.push_back(detection_history_[i]);
    }
    
    return results;
}

void FaultDetectionAdapter::updateModel(
    const std::vector<TimeSeriesData>& training_data) {
    
    if (!vae_model_) {
        std::cerr << "VAE model not initialized" << std::endl;
        return;
    }
    
    // TODO: Implement online learning/model update
    // This would require extending the LinearVAE class
    std::cout << "Model update with " << training_data.size() 
              << " training samples (not implemented)" << std::endl;
}

void FaultDetectionAdapter::setThreshold(double threshold) {
    threshold_ = threshold;
}

std::map<std::string, double> FaultDetectionAdapter::getModelMetrics() const {
    std::map<std::string, double> metrics;
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    metrics["sample_count"] = static_cast<double>(sample_count_);
    metrics["running_mean"] = running_mean_;
    metrics["running_std"] = std::sqrt(running_variance_);
    
    return metrics;
}

// Register the plugin
REGISTER_PLUGIN(FaultDetectionAdapter, "fault_detection")

} // namespace plugins
} // namespace sage_tsdb
