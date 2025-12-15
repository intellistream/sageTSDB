#pragma once

#include "../core/time_series_data.h"
#include "../core/resource_manager.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Plugin configuration
 */
using PluginConfig = std::map<std::string, std::string>;

/**
 * @brief Algorithm result structure
 */
struct AlgorithmResult {
    int64_t timestamp;
    std::map<std::string, double> metrics;
    std::vector<uint8_t> payload;  // For custom data
    
    AlgorithmResult() : timestamp(0) {}
};

/**
 * @brief Abstract interface for external algorithm plugins
 * 
 * This interface provides a decoupled way to integrate external algorithms
 * (like PECJ, Fault Detection) into sageTSDB without tight coupling.
 * 
 * Design Goals:
 * - Zero dependency on sageTSDB internals
 * - Easy to port to other time series databases
 * - Shared resource management
 * - High performance data passing
 */
class IAlgorithmPlugin {
public:
    virtual ~IAlgorithmPlugin() = default;
    
    /**
     * @brief Initialize the plugin with configuration
     * @param config Configuration parameters
     * @return true if initialization succeeds
     */
    virtual bool initialize(const PluginConfig& config) = 0;
    
    /**
     * @brief Initialize the plugin with ResourceManager support (optional)
     * @param config Configuration parameters
     * @param request Resource requirements
     * @param handle ResourceHandle for submitting tasks
     * @return true if initialization succeeds, false to fallback to legacy init
     * 
     * Default implementation returns false, forcing fallback to initialize(config).
     * Plugins supporting ResourceManager should override this method.
     */
    virtual bool initialize(const PluginConfig& config, 
                          const core::ResourceRequest& request,
                          core::ResourceHandle* handle) {
        // Default: not supported, fallback to legacy
        return false;
    }
    
    /**
     * @brief Feed time series data to the plugin
     * @param data Input time series data
     * @note This method should be thread-safe for concurrent data feeding
     */
    virtual void feedData(const TimeSeriesData& data) = 0;
    
    /**
     * @brief Process accumulated data
     * @return Processing results
     */
    virtual AlgorithmResult process() = 0;
    
    /**
     * @brief Get algorithm statistics
     * @return Statistics map (e.g., latency, throughput, accuracy)
     */
    virtual std::map<std::string, int64_t> getStats() const = 0;
    
    /**
     * @brief Reset plugin state
     */
    virtual void reset() = 0;
    
    /**
     * @brief Start the plugin (may spawn threads)
     */
    virtual bool start() = 0;
    
    /**
     * @brief Stop the plugin gracefully
     */
    virtual bool stop() = 0;
    
    /**
     * @brief Get plugin name
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get plugin version
     */
    virtual std::string getVersion() const = 0;
};

using PluginPtr = std::shared_ptr<IAlgorithmPlugin>;

/**
 * @brief Factory function type for creating plugins
 */
using PluginCreator = std::function<PluginPtr(const PluginConfig&)>;

/**
 * @brief Macro to register a plugin
 */
#define REGISTER_PLUGIN(PluginClass, plugin_name)                      \
    namespace {                                                         \
    struct PluginClass##Registrar {                                    \
        PluginClass##Registrar() {                                     \
            sage_tsdb::plugins::PluginRegistry::instance().register_plugin( \
                plugin_name,                                            \
                [](const sage_tsdb::plugins::PluginConfig& cfg) {      \
                    return std::make_shared<PluginClass>(cfg);         \
                });                                                     \
        }                                                               \
    };                                                                  \
    static PluginClass##Registrar global_##PluginClass##Registrar;    \
    }

} // namespace plugins
} // namespace sage_tsdb
