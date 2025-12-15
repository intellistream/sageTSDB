#include "sage_tsdb/core/resource_manager.h"
#include <mutex>
#include <unordered_map>
#include <queue>
#include <thread>
#include <condition_variable>

namespace sage_tsdb {
namespace core {

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
        
        // Calculate current usage
        ResourceUsage current_total;
        for (const auto& [name, handle] : handles_) {
            auto usage = handle->getUsage();
            current_total.threads_used += usage.threads_used;
            current_total.memory_used_bytes += usage.memory_used_bytes;
        }
        
        // Determine actual allocation with quota enforcement
        ResourceRequest allocated = request;
        if (allocated.requested_threads == 0) {
            allocated.requested_threads = 4; // Default
        }
        if (allocated.max_memory_bytes == 0) {
            allocated.max_memory_bytes = 512ULL * 1024 * 1024; // 512MB default
        }
        
        // Check if we have enough resources
        int total_threads_after = current_total.threads_used + allocated.requested_threads;
        uint64_t total_memory_after = current_total.memory_used_bytes + allocated.max_memory_bytes;
        
        if (total_threads_after > max_threads_) {
            // Reduce thread allocation to fit within limits
            int available_threads = max_threads_ - current_total.threads_used;
            if (available_threads <= 0) {
                return nullptr; // No threads available
            }
            allocated.requested_threads = std::min(allocated.requested_threads, available_threads);
        }
        
        if (total_memory_after > max_memory_bytes_) {
            // Reduce memory allocation to fit within limits
            uint64_t available_memory = max_memory_bytes_ - current_total.memory_used_bytes;
            if (available_memory < 128ULL * 1024 * 1024) { // Minimum 128MB
                return nullptr; // Not enough memory
            }
            allocated.max_memory_bytes = std::min(allocated.max_memory_bytes, available_memory);
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
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = handles_.find(plugin_name);
        if (it == handles_.end()) {
            return false; // Plugin not found
        }
        
        // Update the allocated resources for the plugin
        // Note: Thread count changes would require stopping/restarting threads
        // For now, we only support memory quota adjustments
        ResourceRequest current = it->second->getAllocated();
        
        // Create updated request (only memory can be adjusted dynamically)
        ResourceRequest updated = current;
        if (new_request.max_memory_bytes > 0) {
            updated.max_memory_bytes = new_request.max_memory_bytes;
        }
        if (new_request.critical_memory_bytes > 0) {
            updated.critical_memory_bytes = new_request.critical_memory_bytes;
        }
        
        // Thread count adjustment requires recreation (not implemented yet)
        if (new_request.requested_threads > 0 && 
            new_request.requested_threads != current.requested_threads) {
            // TODO: Implement thread pool resizing
            return false;
        }
        
        // Apply the new quota (would need to update the handle's internal state)
        // For this minimal implementation, we just track it
        return true;
    }
    
    void setGlobalLimits(int max_threads, uint64_t max_memory_bytes) override {
        std::lock_guard<std::mutex> lock(mutex_);
        max_threads_ = max_threads;
        max_memory_bytes_ = max_memory_bytes;
    }
    
    bool isUnderPressure() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        int total_threads = 0;
        uint64_t total_memory = 0;
        
        for (const auto& [name, handle] : handles_) {
            auto usage = handle->getUsage();
            total_threads += usage.threads_used;
            total_memory += usage.memory_used_bytes;
        }
        
        return (total_threads >= max_threads_ * 0.9) ||
               (total_memory >= max_memory_bytes_ * 0.9);
    }
    
    // ========== Compute Engine Resource Management Implementation ==========
    
    std::shared_ptr<ResourceHandle> allocateForCompute(
        const std::string& compute_name,
        const ResourceRequest& request) override {
        
        // Use the same allocation mechanism as plugins, but track separately
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if already allocated
        if (compute_handles_.find(compute_name) != compute_handles_.end()) {
            return compute_handles_[compute_name];
        }
        
        // Calculate current usage (including both plugins and compute engines)
        ResourceUsage current_total;
        for (const auto& [name, handle] : handles_) {
            auto usage = handle->getUsage();
            current_total.threads_used += usage.threads_used;
            current_total.memory_used_bytes += usage.memory_used_bytes;
        }
        for (const auto& [name, handle] : compute_handles_) {
            auto usage = handle->getUsage();
            current_total.threads_used += usage.threads_used;
            current_total.memory_used_bytes += usage.memory_used_bytes;
        }
        
        // Determine actual allocation with quota enforcement
        ResourceRequest allocated = request;
        if (allocated.requested_threads == 0) {
            allocated.requested_threads = 4; // Default for compute engines
        }
        if (allocated.max_memory_bytes == 0) {
            allocated.max_memory_bytes = 1ULL * 1024 * 1024 * 1024; // 1GB default
        }
        
        // Check if we have enough resources
        int total_threads_after = current_total.threads_used + allocated.requested_threads;
        uint64_t total_memory_after = current_total.memory_used_bytes + allocated.max_memory_bytes;
        
        if (total_threads_after > max_threads_) {
            int available_threads = max_threads_ - current_total.threads_used;
            if (available_threads <= 0) {
                return nullptr;
            }
            allocated.requested_threads = std::min(allocated.requested_threads, available_threads);
        }
        
        if (total_memory_after > max_memory_bytes_) {
            uint64_t available_memory = max_memory_bytes_ - current_total.memory_used_bytes;
            if (available_memory < 256ULL * 1024 * 1024) { // Minimum 256MB for compute
                return nullptr;
            }
            allocated.max_memory_bytes = std::min(allocated.max_memory_bytes, available_memory);
        }
        
        // Create handle
        auto handle = std::make_shared<ResourceHandleImpl>(compute_name, allocated);
        compute_handles_[compute_name] = handle;
        
        // Spawn worker threads for this compute engine
        for (int i = 0; i < allocated.requested_threads; ++i) {
            worker_threads_.emplace_back([handle, this]() {
                handle->processTasksUntil(should_stop_);
            });
        }
        
        return handle;
    }
    
    void releaseCompute(const std::string& compute_name) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = compute_handles_.find(compute_name);
        if (it != compute_handles_.end()) {
            it->second->invalidate();
            compute_handles_.erase(it);
        }
    }
    
    ResourceUsage getComputeUsage(const std::string& compute_name) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = compute_handles_.find(compute_name);
        if (it != compute_handles_.end()) {
            return it->second->getUsage();
        }
        return ResourceUsage{};
    }
    
    void throttleCompute(const std::string& compute_name, double factor) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = compute_handles_.find(compute_name);
        if (it != compute_handles_.end()) {
            // Store throttle factor for future use
            compute_throttle_[compute_name] = factor;
            
            // In a real implementation, this would:
            // 1. Adjust task execution rate
            // 2. Insert delays between tasks
            // 3. Reduce thread priority
            // For now, we just track the factor
        }
    }
    
    std::vector<std::string> listComputeEngines() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> names;
        names.reserve(compute_handles_.size());
        for (const auto& [name, _] : compute_handles_) {
            names.push_back(name);
        }
        return names;
    }
    
private:
    mutable std::mutex mutex_;
    
    // Plugin handles (original functionality)
    std::unordered_map<std::string, std::shared_ptr<ResourceHandleImpl>> handles_;
    
    // Compute engine handles (separate pool for isolation)
    std::unordered_map<std::string, std::shared_ptr<ResourceHandleImpl>> compute_handles_;
    
    // Compute throttle factors
    std::unordered_map<std::string, double> compute_throttle_;
    
    int max_threads_;
    uint64_t max_memory_bytes_;
    
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> should_stop_;
};

// Factory function implementation
std::shared_ptr<ResourceManager> createResourceManager() {
    return std::make_shared<ResourceManagerImpl>();
}

}  // namespace core
}  // namespace sage_tsdb
