#include "sage_tsdb/plugins/resource_manager.h"
#include <mutex>
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Internal resource handle implementation
 */
class ResourceHandleImpl : public ResourceHandle {
public:
    ResourceHandleImpl(const std::string& name, const ResourceRequest& allocated)
        : plugin_name_(name), allocated_(allocated), valid_(true) {}
    
    ~ResourceHandleImpl() override = default;
    
    bool submitTask(std::function<void()> task) override {
        if (!valid_) return false;
        
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
        queue_cv_.notify_one();
        return true;
    }
    
    bool isValid() const override {
        return valid_.load();
    }
    
    ResourceRequest getAllocated() const override {
        return allocated_;
    }
    
    void reportUsage(const ResourceUsage& usage) override {
        std::lock_guard<std::mutex> lock(usage_mutex_);
        current_usage_ = usage;
    }
    
    ResourceUsage getUsage() const {
        std::lock_guard<std::mutex> lock(usage_mutex_);
        return current_usage_;
    }
    
    void invalidate() {
        valid_.store(false);
        queue_cv_.notify_all();
    }
    
    // Worker thread loop (called by thread pool)
    void processTasksUntil(std::atomic<bool>& should_stop) {
        while (!should_stop.load() && valid_.load()) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, &should_stop]() {
                    return !task_queue_.empty() || should_stop.load() || !valid_.load();
                });
                
                if (task_queue_.empty() || should_stop.load() || !valid_.load()) {
                    continue;
                }
                
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
            
            if (task) {
                try {
                    task();
                } catch (...) {
                    // Log error but continue processing
                }
            }
        }
    }
    
private:
    std::string plugin_name_;
    ResourceRequest allocated_;
    std::atomic<bool> valid_;
    
    // Task queue
    std::queue<std::function<void()>> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Usage tracking
    ResourceUsage current_usage_;
    mutable std::mutex usage_mutex_;
};

/**
 * @brief Concrete ResourceManager implementation
 */
class ResourceManagerImpl : public ResourceManager {
public:
    ResourceManagerImpl() 
        : max_threads_(0), max_memory_bytes_(0), should_stop_(false) {
        // Default limits: use hardware concurrency
        max_threads_ = std::thread::hardware_concurrency();
        max_memory_bytes_ = 4ULL * 1024 * 1024 * 1024; // 4GB default
    }
    
    ~ResourceManagerImpl() override {
        should_stop_.store(true);
        // Wait for worker threads
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    std::shared_ptr<ResourceHandle> allocate(
        const std::string& plugin_name,
        const ResourceRequest& request) override {
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if already allocated
        if (handles_.find(plugin_name) != handles_.end()) {
            return handles_[plugin_name];
        }
        
        // Determine actual allocation (TODO: implement quota logic)
        ResourceRequest allocated = request;
        if (allocated.requested_threads == 0) {
            allocated.requested_threads = 4; // Default
        }
        if (allocated.max_memory_bytes == 0) {
            allocated.max_memory_bytes = 512ULL * 1024 * 1024; // 512MB default
        }
        
        // Create handle
        auto handle = std::make_shared<ResourceHandleImpl>(plugin_name, allocated);
        handles_[plugin_name] = handle;
        
        // Spawn worker threads for this plugin
        for (int i = 0; i < allocated.requested_threads; ++i) {
            worker_threads_.emplace_back([handle, this]() {
                handle->processTasksUntil(should_stop_);
            });
        }
        
        return handle;
    }
    
    void release(const std::string& plugin_name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(plugin_name);
        if (it != handles_.end()) {
            it->second->invalidate();
            handles_.erase(it);
        }
    }
    
    ResourceUsage queryUsage(const std::string& plugin_name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(plugin_name);
        if (it != handles_.end()) {
            return it->second->getUsage();
        }
        return ResourceUsage{};
    }
    
    ResourceUsage getTotalUsage() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        ResourceUsage total;
        for (const auto& [name, handle] : handles_) {
            auto usage = handle->getUsage();
            total.threads_used += usage.threads_used;
            total.memory_used_bytes += usage.memory_used_bytes;
            total.queue_length += usage.queue_length;
            total.tuples_processed += usage.tuples_processed;
            total.errors_count += usage.errors_count;
        }
        return total;
    }
    
    bool adjustQuota(
        const std::string& plugin_name,
        const ResourceRequest& new_request) override {
        // TODO: Implement dynamic quota adjustment
        return false;
    }
    
    void setGlobalLimits(int max_threads, uint64_t max_memory_bytes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        max_threads_ = max_threads;
        max_memory_bytes_ = max_memory_bytes;
    }
    
    bool isUnderPressure() const override {
        auto total = getTotalUsage();
        return (total.threads_used >= max_threads_ * 0.9) ||
               (total.memory_used_bytes >= max_memory_bytes_ * 0.9);
    }
    
private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ResourceHandleImpl>> handles_;
    
    int max_threads_;
    uint64_t max_memory_bytes_;
    
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> should_stop_;
};

// Factory function implementation
std::shared_ptr<ResourceManager> createResourceManager() {
    return std::make_shared<ResourceManagerImpl>();
}

}  // namespace plugins
}  // namespace sage_tsdb
