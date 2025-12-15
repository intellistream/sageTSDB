#pragma once

#include "plugin_interface.h"
#include "plugin_registry.h"
#include "event_bus.h"
#include "../core/resource_manager.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Central manager for all plugins
 * 
 * Responsibilities:
 * - Plugin lifecycle management
 * - Resource coordination
 * - Event bus management
 * - Shared resource allocation
 * 
 * Design Goals:
 * - Allow PECJ and Fault Detection to run simultaneously
 * - Share data streams without duplication
 * - Coordinate thread pools and memory
 */
class PluginManager {
public:
    PluginManager();
    ~PluginManager();
    
    /**
     * @brief Initialize the plugin manager
     */
    bool initialize();
    
    /**
     * @brief Load a plugin by name
     */
    bool loadPlugin(const std::string& name, const PluginConfig& config);
    
    /**
     * @brief Unload a plugin
     */
    bool unloadPlugin(const std::string& name);
    
    /**
     * @brief Start all loaded plugins
     */
    bool startAll();
    
    /**
     * @brief Stop all plugins
     */
    void stopAll();
    
    /**
     * @brief Get a plugin by name
     */
    PluginPtr getPlugin(const std::string& name);
    
    /**
     * @brief Feed data to all plugins
     * Zero-copy: data is shared via shared_ptr
     */
    void feedDataToAll(const std::shared_ptr<TimeSeriesData>& data);
    
    /**
     * @brief Feed data to specific plugin
     */
    void feedDataToPlugin(const std::string& plugin_name,
                         const std::shared_ptr<TimeSeriesData>& data);
    
    /**
     * @brief Get event bus for custom subscriptions
     */
    EventBus& getEventBus() { return event_bus_; }
    
    /**
     * @brief Get statistics from all plugins
     */
    std::map<std::string, std::map<std::string, int64_t>> getAllStats() const;
    
    /**
     * @brief Get list of loaded plugins
     */
    std::vector<std::string> getLoadedPlugins() const;
    
    /**
     * @brief Enable/disable a plugin
     */
    void setPluginEnabled(const std::string& name, bool enabled);
    
    /**
     * @brief Check if plugin is enabled
     */
    bool isPluginEnabled(const std::string& name) const;
    
    /**
     * @brief Configure shared resource limits
     */
    struct ResourceConfig {
        size_t max_memory_mb = 1024;       // Max memory for all plugins
        size_t thread_pool_size = 4;       // Shared thread pool size
        bool enable_zero_copy = true;      // Enable zero-copy data passing
        size_t event_queue_size = 10000;   // Event bus queue size
    };
    
    void setResourceConfig(const ResourceConfig& config);
    ResourceConfig getResourceConfig() const;
    
    /**
     * @brief Get ResourceManager instance (for testing/monitoring)
     */
    core::ResourceManager* getResourceManager() { return resource_manager_.get(); }

private:
    /**
     * @brief Setup event subscriptions for plugins
     */
    void setupEventSubscriptions();
    
    /**
     * @brief Handle data ingestion events
     */
    void handleDataEvent(const Event& event);
    
    // Plugin instances
    std::unordered_map<std::string, PluginPtr> plugins_;
    std::unordered_map<std::string, bool> plugin_enabled_;
    mutable std::mutex plugins_mutex_;
    
    // Resource management per plugin
    std::unordered_map<std::string, std::shared_ptr<core::ResourceHandle>> plugin_resources_;
    std::shared_ptr<core::ResourceManager> resource_manager_;
    
    // Event bus for communication
    EventBus event_bus_;
    std::vector<int> event_subscriptions_;
    
    // Resource management
    ResourceConfig resource_config_;
    mutable std::mutex resource_mutex_;
    
    // State
    bool initialized_;
    bool running_;
};

} // namespace plugins
} // namespace sage_tsdb
