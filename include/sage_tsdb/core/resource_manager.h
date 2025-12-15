#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace sage_tsdb {
namespace core {

/**
 * @brief Resource request descriptor
 * 
 * Used by plugins to request resources from ResourceManager.
 * All fields are hints; actual allocation may differ based on system load.
 */
struct ResourceRequest {
    // Thread allocation
    int requested_threads = 0;  ///< Desired number of worker threads (0 = use default)
    
    // Memory constraints
    uint64_t max_memory_bytes = 0;  ///< Soft memory limit in bytes (0 = unlimited)
    uint64_t critical_memory_bytes = 0;  ///< Hard limit; triggers forced cleanup
    
    // GPU/Device allocation (optional)
    std::vector<int> gpu_ids;  ///< Preferred GPU device IDs (empty = CPU only)
    
    // Model/Asset paths (optional)
    std::string model_path;  ///< Path to ML model file (for caching)
    
    // Priority hint
    int priority = 0;  ///< Higher values = higher priority (default 0)
    
    ResourceRequest() = default;
};

/**
 * @brief Resource usage metrics
 * 
 * Reported by plugins to ResourceManager for monitoring and quota enforcement.
 */
struct ResourceUsage {
    int threads_used = 0;  ///< Current active threads
    uint64_t memory_used_bytes = 0;  ///< Current memory footprint
    uint64_t queue_length = 0;  ///< Pending work items in queue
    
    // Throughput metrics
    uint64_t tuples_processed = 0;  ///< Total tuples/events processed
    double avg_latency_ms = 0.0;  ///< Average processing latency
    
    // Error tracking
    uint64_t errors_count = 0;  ///< Total error count
    std::string last_error;  ///< Last error message (if any)
    
    ResourceUsage() = default;
};

/**
 * @brief Opaque handle to allocated resources
 * 
 * Plugins receive this handle after successful resource allocation.
 * Used to submit tasks, query status, and release resources.
 */
class ResourceHandle {
public:
    virtual ~ResourceHandle() = default;
    
    /**
     * @brief Submit a task to the managed thread pool
     * @param task Callable to execute (void() signature)
     * @return true if task was enqueued successfully
     * 
     * Note: Tasks are executed asynchronously. Use callbacks for completion.
     */
    virtual bool submitTask(std::function<void()> task) = 0;
    
    /**
     * @brief Check if resource allocation is still valid
     * @return true if handle is valid and resources are available
     */
    virtual bool isValid() const = 0;
    
    /**
     * @brief Get allocated resource limits
     * @return Actual allocated resources (may differ from request)
     */
    virtual ResourceRequest getAllocated() const = 0;
    
    /**
     * @brief Report current resource usage
     * @param usage Current usage metrics
     * 
     * Plugins should call this periodically (e.g., every 1-5 seconds).
     */
    virtual void reportUsage(const ResourceUsage& usage) = 0;
};

/**
 * @brief Resource manager interface
 * 
 * Central orchestrator for all plugin resources. Enforces quotas,
 * prevents resource exhaustion, and provides monitoring hooks.
 * 
 * Design:
 * - Single global instance (managed by PluginManager)
 * - Thread-safe allocation/deallocation
 * - Supports degradation (reduce quota or switch to stub mode)
 */
class ResourceManager {
public:
    virtual ~ResourceManager() = default;
    
    /**
     * @brief Allocate resources for a plugin
     * @param plugin_name Unique plugin identifier
     * @param request Resource requirements
     * @return Handle to allocated resources, or nullptr if allocation fails
     * 
     * Thread-safe. May block if resources are temporarily unavailable.
     */
    virtual std::shared_ptr<ResourceHandle> allocate(
        const std::string& plugin_name,
        const ResourceRequest& request) = 0;
    
    /**
     * @brief Release resources (usually called automatically on handle destruction)
     * @param plugin_name Plugin identifier
     */
    virtual void release(const std::string& plugin_name) = 0;
    
    /**
     * @brief Query current usage for a plugin
     * @param plugin_name Plugin identifier
     * @return Current usage metrics, or default if plugin not found
     */
    virtual ResourceUsage queryUsage(const std::string& plugin_name) const = 0;
    
    /**
     * @brief Get global resource statistics
     * @return Total usage across all plugins
     */
    virtual ResourceUsage getTotalUsage() const = 0;
    
    /**
     * @brief Adjust quota for an already-allocated plugin
     * @param plugin_name Plugin identifier
     * @param new_request Updated resource limits
     * @return true if adjustment succeeded
     * 
     * Used for runtime tuning or degradation strategies.
     */
    virtual bool adjustQuota(
        const std::string& plugin_name,
        const ResourceRequest& new_request) = 0;
    
    /**
     * @brief Set global resource limits
     * @param max_threads Max threads across all plugins
     * @param max_memory_bytes Max memory across all plugins
     */
    virtual void setGlobalLimits(int max_threads, uint64_t max_memory_bytes) = 0;
    
    /**
     * @brief Check if system is under resource pressure
     * @return true if close to global limits (triggers degradation)
     */
    virtual bool isUnderPressure() const = 0;
    
    // ========== Compute Engine Resource Management ==========
    
    /**
     * @brief Allocate resources for a compute engine
     * @param compute_name Compute engine identifier (e.g., "pecj_engine")
     * @param request Resource requirements
     * @return Handle to allocated resources, or nullptr if allocation fails
     * 
     * This is a specialized allocation method for compute engines like PECJ.
     * Key differences from plugin allocation:
     * - Stricter resource isolation (separate quota pool)
     * - Task-based execution (submitTask instead of continuous threads)
     * - Integration with ComputeStateManager for state persistence
     * 
     * Thread-safe. May block if resources are temporarily unavailable.
     * 
     * Example:
     *   ResourceRequest req;
     *   req.requested_threads = 4;
     *   req.max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 2GB
     *   auto handle = rm->allocateForCompute("pecj_engine", req);
     *   if (handle) {
     *       handle->submitTask([](){ // compute task
     *       });
     *   }
     */
    virtual std::shared_ptr<ResourceHandle> allocateForCompute(
        const std::string& compute_name,
        const ResourceRequest& request) = 0;
    
    /**
     * @brief Release compute engine resources
     * @param compute_name Compute engine identifier
     * 
     * Usually called automatically when ResourceHandle is destroyed.
     */
    virtual void releaseCompute(const std::string& compute_name) = 0;
    
    /**
     * @brief Query resource usage for a compute engine
     * @param compute_name Compute engine identifier
     * @return Current usage metrics, or default if not found
     */
    virtual ResourceUsage getComputeUsage(const std::string& compute_name) const = 0;
    
    /**
     * @brief Force throttle a compute engine
     * @param compute_name Compute engine identifier
     * @param factor Throttle factor (0.0 = pause, 1.0 = no throttle)
     * 
     * Used when compute engine consumes too many resources.
     * Example: factor = 0.5 means reduce throughput to 50%.
     */
    virtual void throttleCompute(const std::string& compute_name, double factor) = 0;
    
    /**
     * @brief List all active compute engines
     * @return Vector of compute engine names
     */
    virtual std::vector<std::string> listComputeEngines() const = 0;
};

/**
 * @brief Factory function to create ResourceManager instance
 * @return Pointer to concrete ResourceManager implementation
 * 
 * Usage:
 *   auto rm = createResourceManager();
 *   rm->setGlobalLimits(16, 4ULL * 1024 * 1024 * 1024); // 16 threads, 4GB
 */
std::shared_ptr<ResourceManager> createResourceManager();

}  // namespace core
}  // namespace sage_tsdb
