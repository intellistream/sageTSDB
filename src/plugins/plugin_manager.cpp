#include "sage_tsdb/plugins/plugin_manager.h"
#include <iostream>
#include <sstream>

namespace sage_tsdb {
namespace plugins {

namespace {
// Helper to get config value with default
template<typename T>
T getConfigValue(const PluginConfig& config, const std::string& key, T default_value) {
    auto it = config.find(key);
    if (it == config.end()) {
        return default_value;
    }
    
    std::istringstream iss(it->second);
    T value;
    if (iss >> value) {
        return value;
    }
    return default_value;
}
} // anonymous namespace

PluginManager::PluginManager()
    : initialized_(false), running_(false) {
}

PluginManager::~PluginManager() {
    stopAll();
}

bool PluginManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Create ResourceManager (now in core namespace)
    resource_manager_ = core::createResourceManager();
    if (!resource_manager_) {
        std::cerr << "Failed to create ResourceManager" << std::endl;
        return false;
    }
    
    // Set global limits from config
    resource_manager_->setGlobalLimits(
        resource_config_.thread_pool_size,
        resource_config_.max_memory_mb * 1024ULL * 1024ULL
    );
    
    std::cout << "✓ ResourceManager created (threads=" << resource_config_.thread_pool_size 
              << ", memory=" << resource_config_.max_memory_mb << "MB)" << std::endl;
    
    // Start event bus
    event_bus_.start();
    
    // Setup event subscriptions for plugin coordination
    setupEventSubscriptions();
    
    initialized_ = true;
    return true;
}

bool PluginManager::loadPlugin(const std::string& name, const PluginConfig& config) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    // Check if plugin already loaded
    if (plugins_.find(name) != plugins_.end()) {
        std::cerr << "Plugin '" << name << "' already loaded" << std::endl;
        return false;
    }
    
    // Create plugin using registry
    auto plugin = PluginRegistry::instance().create_plugin(name, config);
    if (!plugin) {
        std::cerr << "Failed to create plugin '" << name << "'" << std::endl;
        return false;
    }
    
    // Attempt resource allocation if ResourceManager is available
    std::shared_ptr<core::ResourceHandle> resource_handle;
    core::ResourceRequest request;  // Reuse for both allocation and initialization
    
    if (resource_manager_) {
        // Extract resource request from plugin config (with defaults)
        request.requested_threads = getConfigValue<int>(config, "threads", 2);
        request.max_memory_bytes = getConfigValue<size_t>(config, "memory_mb", 256) * 1024ULL * 1024ULL;
        request.priority = getConfigValue<int>(config, "priority", 1);
        
        // GPU support (optional)
        auto gpu_id = getConfigValue<int>(config, "gpu_id", -1);
        if (gpu_id >= 0) {
            request.gpu_ids.push_back(gpu_id);
        }
        
        resource_handle = resource_manager_->allocate(name, request);
        if (!resource_handle) {
            std::cerr << "Failed to allocate resources for plugin '" << name << "'" << std::endl;
            // Continue with legacy initialization
        } else {
            std::cout << "✓ Resources allocated for '" << name << "' (threads=" 
                      << request.requested_threads << ", memory=" 
                      << (request.max_memory_bytes / 1024 / 1024) << "MB)" << std::endl;
        }
    }
    
    // Try new initialize method with ResourceManager first
    bool init_success = false;
    if (resource_handle) {
        init_success = plugin->initialize(config, request, resource_handle.get());
        
        if (init_success) {
            std::cout << "✓ Plugin '" << name << "' initialized in Integrated mode" << std::endl;
            // Store the resource handle for later cleanup
            plugin_resources_[name] = std::move(resource_handle);
        } else {
            std::cerr << "Plugin '" << name << "' rejected ResourceManager initialization" << std::endl;
            // Release the resource handle and fall back
            resource_handle.reset();
        }
    }
    
    // Fallback to legacy initialization
    if (!init_success) {
        if (!plugin->initialize(config)) {
            std::cerr << "Failed to initialize plugin '" << name << "'" << std::endl;
            return false;
        }
        std::cout << "✓ Plugin '" << name << "' initialized in Baseline/Stub mode" << std::endl;
    }
    
    // Store plugin
    plugins_[name] = plugin;
    plugin_enabled_[name] = true;
    
    std::cout << "✓ Plugin '" << name << "' loaded successfully" << std::endl;
    return true;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return false;
    }
    
    // Stop plugin if running
    it->second->stop();
    
    // Release resources if allocated
    auto res_it = plugin_resources_.find(name);
    if (res_it != plugin_resources_.end()) {
        if (resource_manager_) {
            resource_manager_->release(name);
            std::cout << "✓ Resources released for plugin '" << name << "'" << std::endl;
        }
        plugin_resources_.erase(res_it);
    }
    
    // Remove plugin
    plugins_.erase(it);
    plugin_enabled_.erase(name);
    
    std::cout << "✓ Plugin '" << name << "' unloaded" << std::endl;
    return true;
}

bool PluginManager::startAll() {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    bool all_started = true;
    for (auto& pair : plugins_) {
        if (plugin_enabled_[pair.first]) {
            if (!pair.second->start()) {
                std::cerr << "Failed to start plugin '" << pair.first << "'" << std::endl;
                all_started = false;
            } else {
                std::cout << "✓ Plugin '" << pair.first << "' started" << std::endl;
            }
        }
    }
    
    if (all_started) {
        running_ = true;
    }
    
    return all_started;
}

void PluginManager::stopAll() {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    for (auto& pair : plugins_) {
        pair.second->stop();
        std::cout << "✓ Plugin '" << pair.first << "' stopped" << std::endl;
    }
    
    running_ = false;
    event_bus_.stop();
}

PluginPtr PluginManager::getPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(name);
    if (it != plugins_.end()) {
        return it->second;
    }
    
    return nullptr;
}

void PluginManager::feedDataToAll(const std::shared_ptr<TimeSeriesData>& data) {
    if (!running_) {
        return;
    }
    
    // Publish to event bus (zero-copy via shared_ptr)
    event_bus_.publish_data(data, "core");
    
    // Direct feed to all enabled plugins
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    for (auto& pair : plugins_) {
        if (plugin_enabled_[pair.first]) {
            try {
                pair.second->feedData(*data);
            } catch (const std::exception& e) {
                std::cerr << "Error feeding data to plugin '" << pair.first 
                         << "': " << e.what() << std::endl;
            }
        }
    }
}

void PluginManager::feedDataToPlugin(const std::string& plugin_name,
                                    const std::shared_ptr<TimeSeriesData>& data) {
    if (!running_) {
        return;
    }
    
    auto plugin = getPlugin(plugin_name);
    if (plugin && isPluginEnabled(plugin_name)) {
        try {
            plugin->feedData(*data);
        } catch (const std::exception& e) {
            std::cerr << "Error feeding data to plugin '" << plugin_name 
                     << "': " << e.what() << std::endl;
        }
    }
}

std::map<std::string, std::map<std::string, int64_t>> 
PluginManager::getAllStats() const {
    std::map<std::string, std::map<std::string, int64_t>> all_stats;
    
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    for (const auto& pair : plugins_) {
        try {
            all_stats[pair.first] = pair.second->getStats();
            
            // Add resource usage if ResourceManager is active
            auto res_it = plugin_resources_.find(pair.first);
            if (res_it != plugin_resources_.end() && resource_manager_) {
                auto usage = resource_manager_->queryUsage(pair.first);
                all_stats[pair.first]["resource_threads"] = usage.threads_used;
                all_stats[pair.first]["resource_memory_mb"] = usage.memory_used_bytes / 1024 / 1024;
                all_stats[pair.first]["resource_queue_length"] = usage.queue_length;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error getting stats from plugin '" << pair.first 
                     << "': " << e.what() << std::endl;
        }
    }
    
    // Add global ResourceManager stats
    if (resource_manager_) {
        auto total_usage = resource_manager_->getTotalUsage();
        all_stats["_resource_manager"]["total_threads"] = total_usage.threads_used;
        all_stats["_resource_manager"]["total_memory_mb"] = total_usage.memory_used_bytes / 1024 / 1024;
        all_stats["_resource_manager"]["total_queue_length"] = total_usage.queue_length;
        all_stats["_resource_manager"]["high_pressure"] = 
            resource_manager_->isUnderPressure() ? 1 : 0;
    }
    
    return all_stats;
}

std::vector<std::string> PluginManager::getLoadedPlugins() const {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    std::vector<std::string> plugin_names;
    for (const auto& pair : plugins_) {
        plugin_names.push_back(pair.first);
    }
    
    return plugin_names;
}

void PluginManager::setPluginEnabled(const std::string& name, bool enabled) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    if (plugins_.find(name) != plugins_.end()) {
        plugin_enabled_[name] = enabled;
        std::cout << "Plugin '" << name << "' " 
                  << (enabled ? "enabled" : "disabled") << std::endl;
    }
}

bool PluginManager::isPluginEnabled(const std::string& name) const {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugin_enabled_.find(name);
    if (it != plugin_enabled_.end()) {
        return it->second;
    }
    
    return false;
}

void PluginManager::setResourceConfig(const ResourceConfig& config) {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    resource_config_ = config;
}

PluginManager::ResourceConfig PluginManager::getResourceConfig() const {
    std::lock_guard<std::mutex> lock(resource_mutex_);
    return resource_config_;
}

void PluginManager::setupEventSubscriptions() {
    // Subscribe to data ingestion events
    int sub_id = event_bus_.subscribe(EventType::DATA_INGESTED, 
        [this](const Event& event) {
            this->handleDataEvent(event);
        });
    event_subscriptions_.push_back(sub_id);
    
    // Subscribe to result events for logging/monitoring
    sub_id = event_bus_.subscribe(EventType::RESULT_READY,
        [](const Event& event) {
            // Could log or forward results
            // std::cout << "Result ready from: " << event.source << std::endl;
        });
    event_subscriptions_.push_back(sub_id);
}

void PluginManager::handleDataEvent(const Event& event) {
    // Extract data from event
    auto data = std::static_pointer_cast<TimeSeriesData>(event.payload);
    if (!data) {
        return;
    }
    
    // Distribute to all plugins (already handled by feedDataToAll)
    // This is for additional event-based processing if needed
}

} // namespace plugins
} // namespace sage_tsdb
