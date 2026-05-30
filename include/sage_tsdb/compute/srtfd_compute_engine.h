/**
 * @file srtfd_compute_engine.h
 * @brief Stateless SRTFD fault diagnosis compute engine.
 */

#pragma once

#include "sage_tsdb/core/time_series_data.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <shared_mutex>
#include <string>
#include <vector>

namespace sage_tsdb {

class TimeSeriesDB;

namespace core {
class ResourceHandle;
}

namespace compute {

enum class SRTFDBackend {
    Statistical,
    TorchScript,
    External
};

std::string srtfdBackendToString(SRTFDBackend backend);
SRTFDBackend stringToSRTFDBackend(const std::string& backend);

struct SRTFDConfig {
    std::string backend = "statistical";
    std::string model_path;
    std::string dataset = "TEP";

    std::string input_table = "sensor_events";
    std::string result_table = "srtfd_results";

    size_t input_dim = 52;
    size_t num_classes = 22;
    bool strict_input_dim = true;
    bool write_results = true;

    double anomaly_threshold = 3.0;
    size_t max_memory_bytes = 512ULL * 1024 * 1024;
    int max_threads = 1;
};

struct SRTFDDiagnosis {
    int64_t timestamp = 0;
    size_t sample_index = 0;
    int predicted_class = 0;
    double confidence = 0.0;
    double anomaly_score = 0.0;
    bool is_anomaly = false;
    std::string asset_id;
    std::string backend;
    std::map<std::string, double> features;
};

struct SRTFDStatus {
    bool success = false;
    std::string error;
    uint64_t window_id = 0;

    size_t input_count = 0;
    size_t result_count = 0;
    size_t invalid_count = 0;
    size_t anomaly_count = 0;

    double avg_confidence = 0.0;
    double max_anomaly_score = 0.0;
    double computation_time_ms = 0.0;
    std::string backend;
};

struct SRTFDMetrics {
    uint64_t total_windows_completed = 0;
    uint64_t failed_windows = 0;
    uint64_t total_samples_processed = 0;
    uint64_t total_results_written = 0;
    uint64_t total_invalid_samples = 0;
    uint64_t total_anomalies = 0;

    double avg_window_latency_ms = 0.0;
    double min_window_latency_ms = 0.0;
    double max_window_latency_ms = 0.0;
    double p99_window_latency_ms = 0.0;
    double avg_confidence = 0.0;
    double anomaly_rate = 0.0;
};

class SRTFDComputeEngine {
public:
    SRTFDComputeEngine();
    ~SRTFDComputeEngine() = default;

    SRTFDComputeEngine(const SRTFDComputeEngine&) = delete;
    SRTFDComputeEngine& operator=(const SRTFDComputeEngine&) = delete;
    SRTFDComputeEngine(SRTFDComputeEngine&&) = delete;
    SRTFDComputeEngine& operator=(SRTFDComputeEngine&&) = delete;

    bool initialize(const SRTFDConfig& config,
                    TimeSeriesDB* db,
                    core::ResourceHandle* resource_handle);

    SRTFDStatus executeWindowDiagnosis(uint64_t window_id,
                                       const sage_tsdb::TimeRange& time_range);

    SRTFDMetrics getMetrics() const;
    void reset();

    bool isInitialized() const { return initialized_.load(); }
    const SRTFDConfig& getConfig() const { return config_; }

private:
    TimeSeriesDB* db_;
    core::ResourceHandle* resource_handle_;
    SRTFDConfig config_;
    SRTFDBackend backend_;
    std::atomic<bool> initialized_;
    std::atomic<size_t> current_memory_usage_;

    mutable std::shared_mutex metrics_mutex_;
    SRTFDMetrics metrics_;
    std::vector<double> latency_samples_;

    bool validateConfig() const;
    bool validateBackend() const;
    bool validateFeatures(const std::vector<double>& features) const;
    SRTFDDiagnosis diagnoseSample(const TimeSeriesData& data,
                                  const std::vector<double>& features,
                                  size_t sample_index) const;
    SRTFDDiagnosis runStatisticalBackend(const TimeSeriesData& data,
                                         const std::vector<double>& features,
                                         size_t sample_index) const;
    bool writeResult(uint64_t window_id, const SRTFDDiagnosis& diagnosis);
    void updateMetrics(const SRTFDStatus& status);
};

} // namespace compute
} // namespace sage_tsdb