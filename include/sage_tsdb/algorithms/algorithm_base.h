#pragma once

#include "../core/time_series_data.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sage_tsdb {

/**
 * @brief Algorithm configuration
 */
using AlgorithmConfig = std::map<std::string, std::string>;

/**
 * @brief Base class for time series processing algorithms
 * 
 * All algorithms should inherit from this class and implement
 * the process() method. Supports:
 * - Configuration through key-value parameters
 * - State management for stateful algorithms
 * - Statistics tracking
 */
class TimeSeriesAlgorithm {
public:
    explicit TimeSeriesAlgorithm(const AlgorithmConfig& config = {})
        : config_(config) {}
    
    virtual ~TimeSeriesAlgorithm() = default;
    
    /**
     * @brief Process time series data
     * @param input Input time series data
     * @return Processed output data
     */
    virtual std::vector<TimeSeriesData> process(
        const std::vector<TimeSeriesData>& input) = 0;
    
    /**
     * @brief Reset algorithm state
     */
    virtual void reset() {
        // Default: no state to reset
    }
    
    /**
     * @brief Get algorithm statistics
     * @return Statistics map
     */
    virtual std::map<std::string, int64_t> get_stats() const {
        return {};
    }
    
    /**
     * @brief Get algorithm configuration
     */
    const AlgorithmConfig& get_config() const {
        return config_;
    }
    
    /**
     * @brief Set configuration parameter
     */
    void set_config(const std::string& key, const std::string& value) {
        config_[key] = value;
    }
    
    /**
     * @brief Get configuration parameter
     */
    std::string get_config(const std::string& key,
                          const std::string& default_value = "") const {
        auto it = config_.find(key);
        return (it != config_.end()) ? it->second : default_value;
    }

protected:
    AlgorithmConfig config_;
};

/**
 * @brief Algorithm factory for registration
 */
class AlgorithmFactory {
public:
    using Creator = std::function<std::shared_ptr<TimeSeriesAlgorithm>(
        const AlgorithmConfig&)>;
    
    static AlgorithmFactory& instance() {
        static AlgorithmFactory factory;
        return factory;
    }
    
    void register_algorithm(const std::string& name, Creator creator) {
        creators_[name] = creator;
    }
    
    std::shared_ptr<TimeSeriesAlgorithm> create(
        const std::string& name,
        const AlgorithmConfig& config = {}) const {
        auto it = creators_.find(name);
        if (it != creators_.end()) {
            return it->second(config);
        }
        return nullptr;
    }
    
    std::vector<std::string> list_algorithms() const {
        std::vector<std::string> names;
        for (const auto& pair : creators_) {
            names.push_back(pair.first);
        }
        return names;
    }

private:
    AlgorithmFactory() = default;
    std::map<std::string, Creator> creators_;
};

/**
 * @brief Helper macro for algorithm registration
 */
#define REGISTER_ALGORITHM(name, class_name)                        \
    namespace {                                                     \
    struct class_name##_Registrar {                                \
        class_name##_Registrar() {                                 \
            AlgorithmFactory::instance().register_algorithm(        \
                name,                                               \
                [](const AlgorithmConfig& config) {                 \
                    return std::make_shared<class_name>(config);    \
                });                                                 \
        }                                                           \
    };                                                              \
    static class_name##_Registrar class_name##_registrar_instance; \
    }

} // namespace sage_tsdb
