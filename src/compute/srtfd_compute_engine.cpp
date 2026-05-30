#include "sage_tsdb/compute/srtfd_compute_engine.h"

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/core/resource_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace sage_tsdb {
namespace compute {

namespace {
constexpr size_t MAX_LATENCY_SAMPLES = 1000;
constexpr double EPSILON = 1e-12;

double clampDouble(double value, double low, double high) {
    return std::max(low, std::min(value, high));
}

double calculatePercentile(const std::vector<double>& sorted_samples, double percentile) {
    if (sorted_samples.empty()) {
        return 0.0;
    }

    size_t index = static_cast<size_t>(sorted_samples.size() * percentile);
    if (index >= sorted_samples.size()) {
        index = sorted_samples.size() - 1;
    }
    return sorted_samples[index];
}

std::string boolToString(bool value) {
    return value ? "true" : "false";
}

} // namespace

std::string srtfdBackendToString(SRTFDBackend backend) {
    switch (backend) {
        case SRTFDBackend::Statistical:
            return "statistical";
        case SRTFDBackend::TorchScript:
            return "torchscript";
        case SRTFDBackend::External:
            return "external";
        default:
            return "statistical";
    }
}

SRTFDBackend stringToSRTFDBackend(const std::string& backend) {
    if (backend == "statistical") {
        return SRTFDBackend::Statistical;
    }
    if (backend == "torchscript") {
        return SRTFDBackend::TorchScript;
    }
    if (backend == "external") {
        return SRTFDBackend::External;
    }
    return SRTFDBackend::Statistical;
}

SRTFDComputeEngine::SRTFDComputeEngine()
    : db_(nullptr),
      resource_handle_(nullptr),
      backend_(SRTFDBackend::Statistical),
      initialized_(false),
      current_memory_usage_(0) {}

bool SRTFDComputeEngine::initialize(const SRTFDConfig& config,
                                    TimeSeriesDB* db,
                                    core::ResourceHandle* resource_handle) {
    if (initialized_.load()) {
        return false;
    }
    if (!db) {
        return false;
    }

    config_ = config;
    db_ = db;
    resource_handle_ = resource_handle;
    backend_ = stringToSRTFDBackend(config_.backend);

    if (!validateConfig() || !validateBackend()) {
        return false;
    }
    if (!db_->hasTable(config_.input_table)) {
        return false;
    }
    if (config_.write_results && !db_->hasTable(config_.result_table)) {
        db_->createTable(config_.result_table, TableType::JoinResult);
    }

    metrics_ = SRTFDMetrics{};
    latency_samples_.reserve(MAX_LATENCY_SAMPLES);
    initialized_.store(true);
    return true;
}

bool SRTFDComputeEngine::validateConfig() const {
    return !config_.input_table.empty()
        && !config_.result_table.empty()
        && config_.input_dim > 0
        && config_.num_classes > 0
        && config_.anomaly_threshold > 0.0
        && config_.max_threads > 0;
}

bool SRTFDComputeEngine::validateBackend() const {
    if (backend_ == SRTFDBackend::Statistical) {
        return true;
    }
    return false;
}

SRTFDStatus SRTFDComputeEngine::executeWindowDiagnosis(
    uint64_t window_id,
    const sage_tsdb::TimeRange& time_range) {
    SRTFDStatus status;
    status.window_id = window_id;
    status.backend = srtfdBackendToString(backend_);

    if (!initialized_.load()) {
        status.error = "Engine not initialized";
        updateMetrics(status);
        return status;
    }
    if (time_range.end_time < time_range.start_time) {
        status.error = "Invalid time range";
        updateMetrics(status);
        return status;
    }

    auto start_time = std::chrono::steady_clock::now();

    try {
        auto input_data = db_->query(config_.input_table, time_range);
        status.input_count = input_data.size();

        size_t estimated_memory = status.input_count * config_.input_dim * sizeof(double);
        current_memory_usage_.store(estimated_memory);
        if (config_.max_memory_bytes > 0 && estimated_memory > config_.max_memory_bytes) {
            std::ostringstream error;
            error << "Estimated input memory " << estimated_memory
                  << " exceeds limit " << config_.max_memory_bytes;
            status.error = error.str();
            updateMetrics(status);
            return status;
        }

        double confidence_sum = 0.0;
        for (size_t index = 0; index < input_data.size(); ++index) {
            auto features = input_data[index].as_vector();
            if (!validateFeatures(features)) {
                ++status.invalid_count;
                continue;
            }

            auto diagnosis = diagnoseSample(input_data[index], features, index);
            if (config_.write_results && !writeResult(window_id, diagnosis)) {
                status.error = "Failed to write SRTFD result";
                updateMetrics(status);
                return status;
            }

            ++status.result_count;
            if (diagnosis.is_anomaly) {
                ++status.anomaly_count;
            }
            confidence_sum += diagnosis.confidence;
            status.max_anomaly_score = std::max(status.max_anomaly_score,
                                                diagnosis.anomaly_score);
        }

        if (status.result_count > 0) {
            status.avg_confidence = confidence_sum / status.result_count;
        }

        auto end_time = std::chrono::steady_clock::now();
        status.computation_time_ms = std::chrono::duration<double, std::milli>(
            end_time - start_time).count();
        status.success = true;
        updateMetrics(status);
        return status;
    } catch (const std::exception& exception) {
        status.error = std::string("Exception: ") + exception.what();
        updateMetrics(status);
        return status;
    }
}

bool SRTFDComputeEngine::validateFeatures(const std::vector<double>& features) const {
    if (features.empty()) {
        return false;
    }
    if (config_.strict_input_dim && features.size() != config_.input_dim) {
        return false;
    }
    return std::all_of(features.begin(), features.end(), [](double value) {
        return std::isfinite(value);
    });
}

SRTFDDiagnosis SRTFDComputeEngine::diagnoseSample(
    const TimeSeriesData& data,
    const std::vector<double>& features,
    size_t sample_index) const {
    switch (backend_) {
        case SRTFDBackend::Statistical:
            return runStatisticalBackend(data, features, sample_index);
        case SRTFDBackend::TorchScript:
        case SRTFDBackend::External:
        default:
            throw std::runtime_error("Configured SRTFD backend is not implemented");
    }
}

SRTFDDiagnosis SRTFDComputeEngine::runStatisticalBackend(
    const TimeSeriesData& data,
    const std::vector<double>& features,
    size_t sample_index) const {
    SRTFDDiagnosis diagnosis;
    diagnosis.timestamp = data.timestamp;
    diagnosis.sample_index = sample_index;
    diagnosis.backend = srtfdBackendToString(backend_);

    auto asset_it = data.tags.find("asset_id");
    if (asset_it != data.tags.end()) {
        diagnosis.asset_id = asset_it->second;
    }

    double sum = std::accumulate(features.begin(), features.end(), 0.0);
    double mean = sum / features.size();
    double abs_sum = 0.0;
    double squared_sum = 0.0;
    double max_abs = 0.0;

    for (double value : features) {
        double abs_value = std::abs(value);
        abs_sum += abs_value;
        squared_sum += value * value;
        max_abs = std::max(max_abs, abs_value);
    }

    double mean_abs = abs_sum / features.size();
    double rms = std::sqrt(squared_sum / features.size());
    double normalized_score = rms / std::max(config_.anomaly_threshold, EPSILON);

    diagnosis.anomaly_score = rms;
    diagnosis.is_anomaly = rms >= config_.anomaly_threshold;

    if (!diagnosis.is_anomaly || config_.num_classes == 1) {
        diagnosis.predicted_class = 0;
    } else {
        double scaled = std::max(0.0, normalized_score - 1.0);
        int class_id = 1 + static_cast<int>(scaled * static_cast<double>(config_.num_classes - 1));
        diagnosis.predicted_class = std::min<int>(class_id, static_cast<int>(config_.num_classes - 1));
    }

    double distance_from_boundary = std::abs(normalized_score - 1.0);
    diagnosis.confidence = clampDouble(0.5 + distance_from_boundary * 0.5, 0.5, 0.999);
    diagnosis.features = {
        {"mean", mean},
        {"mean_abs", mean_abs},
        {"rms", rms},
        {"max_abs", max_abs},
        {"normalized_score", normalized_score}
    };

    return diagnosis;
}

bool SRTFDComputeEngine::writeResult(uint64_t window_id,
                                     const SRTFDDiagnosis& diagnosis) {
    try {
        TimeSeriesData result(diagnosis.timestamp,
                              static_cast<double>(diagnosis.predicted_class));
        result.tags["operator"] = "srtfd";
        result.tags["window_id"] = std::to_string(window_id);
        result.tags["backend"] = diagnosis.backend;
        if (!diagnosis.asset_id.empty()) {
            result.tags["asset_id"] = diagnosis.asset_id;
        }

        result.fields["fault_class"] = std::to_string(diagnosis.predicted_class);
        result.fields["confidence"] = std::to_string(diagnosis.confidence);
        result.fields["anomaly_score"] = std::to_string(diagnosis.anomaly_score);
        result.fields["is_anomaly"] = boolToString(diagnosis.is_anomaly);
        result.fields["sample_index"] = std::to_string(diagnosis.sample_index);
        for (const auto& [name, value] : diagnosis.features) {
            result.fields["feature_" + name] = std::to_string(value);
        }

        db_->insert(config_.result_table, result);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void SRTFDComputeEngine::updateMetrics(const SRTFDStatus& status) {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);

    if (status.success) {
        metrics_.total_windows_completed++;
        metrics_.total_samples_processed += status.input_count;
        metrics_.total_results_written += status.result_count;
        metrics_.total_invalid_samples += status.invalid_count;
        metrics_.total_anomalies += status.anomaly_count;

        if (status.computation_time_ms > 0.0) {
            latency_samples_.push_back(status.computation_time_ms);
            if (latency_samples_.size() > MAX_LATENCY_SAMPLES) {
                latency_samples_.erase(latency_samples_.begin());
            }

            auto sorted_samples = latency_samples_;
            std::sort(sorted_samples.begin(), sorted_samples.end());
            metrics_.avg_window_latency_ms = std::accumulate(
                sorted_samples.begin(), sorted_samples.end(), 0.0) / sorted_samples.size();
            metrics_.min_window_latency_ms = sorted_samples.front();
            metrics_.max_window_latency_ms = sorted_samples.back();
            metrics_.p99_window_latency_ms = calculatePercentile(sorted_samples, 0.99);
        }

        if (status.result_count > 0) {
            double previous_weight = static_cast<double>(metrics_.total_results_written - status.result_count);
            double total_confidence = metrics_.avg_confidence * previous_weight
                                    + status.avg_confidence * status.result_count;
            metrics_.avg_confidence = total_confidence / metrics_.total_results_written;
        }
        if (metrics_.total_results_written > 0) {
            metrics_.anomaly_rate = static_cast<double>(metrics_.total_anomalies)
                                  / static_cast<double>(metrics_.total_results_written);
        }
    } else {
        metrics_.failed_windows++;
    }
}

SRTFDMetrics SRTFDComputeEngine::getMetrics() const {
    std::shared_lock<std::shared_mutex> lock(metrics_mutex_);
    return metrics_;
}

void SRTFDComputeEngine::reset() {
    std::unique_lock<std::shared_mutex> lock(metrics_mutex_);
    metrics_ = SRTFDMetrics{};
    latency_samples_.clear();
    current_memory_usage_.store(0);
}

} // namespace compute
} // namespace sage_tsdb