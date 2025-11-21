#pragma once

#include "plugin_interface.h"
#include <mutex>
#include <unordered_map>

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Registry for managing algorithm plugins
 * 
 * Singleton pattern for centralized plugin management
 */
class PluginRegistry {
public:
    static PluginRegistry& instance() {
        static PluginRegistry registry;
        return registry;
    }
    
    /**
     * @brief Register a plugin creator
     */
    void register_plugin(const std::string& name, PluginCreator creator) {
        std::lock_guard<std::mutex> lock(mutex_);
        creators_[name] = creator;
    }
    
    /**
     * @brief Create a plugin instance
     */
    PluginPtr create_plugin(const std::string& name, const PluginConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = creators_.find(name);
        if (it != creators_.end()) {
            return it->second(config);
        }
        return nullptr;
    }
    
    /**
     * @brief Get all registered plugin names
     */
    std::vector<std::string> get_plugin_names() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        for (const auto& pair : creators_) {
            names.push_back(pair.first);
        }
        return names;
    }
    
    /**
     * @brief Check if a plugin is registered
     */
    bool has_plugin(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return creators_.find(name) != creators_.end();
    }

private:
    PluginRegistry() = default;
    ~PluginRegistry() = default;
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PluginCreator> creators_;
};

} // namespace plugins
} // namespace sage_tsdb
