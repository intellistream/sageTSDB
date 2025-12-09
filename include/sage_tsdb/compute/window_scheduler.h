/**
 * @file window_scheduler.h
 * @brief Window Scheduler for PECJ Deep Integration
 * 
 * This component automatically triggers window computations based on:
 * - Time-based triggers (tumbling/sliding windows)
 * - Data volume triggers (count-based windows)
 * - Event-driven triggers (table insertion notifications)
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-05
 */

#pragma once

#include "pecj_compute_engine.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <queue>
#include <atomic>

#ifdef PECJ_MODE_INTEGRATED

namespace sage_tsdb {
namespace core {
class TableManager;
class StreamTable;
}

namespace plugins {
class ResourceHandle;
}
}

namespace sage_tsdb {
namespace compute {

/**
 * @brief Window trigger policy
 */
enum class TriggerPolicy {
    TimeBased,      ///< Trigger based on wall-clock time
    CountBased,     ///< Trigger based on data count
    Hybrid,         ///< Trigger when either condition is met
    Manual          ///< Manual trigger only (for testing)
};

/**
 * @brief Window type
 */
enum class WindowType {
    Tumbling,       ///< Non-overlapping windows
    Sliding,        ///< Overlapping windows with slide interval
    Session         ///< Session windows (gap-based)
};

/**
 * @brief Window scheduling configuration
 */
struct WindowSchedulerConfig {
    // Window parameters
    WindowType window_type = WindowType::Sliding;
    uint64_t window_len_us = 1000000;     ///< Window length (1s default)
    uint64_t slide_len_us = 500000;       ///< Slide length (500ms default)
    
    // Trigger policy
    TriggerPolicy trigger_policy = TriggerPolicy::Hybrid;
    uint64_t trigger_interval_us = 100000; ///< Check interval (100ms)
    size_t trigger_count_threshold = 1000; ///< Minimum tuples per window
    
    // Scheduling parameters
    uint64_t max_delay_us = 100000;       ///< Maximum allowed delay
    uint64_t watermark_slack_us = 50000;  ///< Watermark slack (50ms)
    bool allow_late_data = true;          ///< Process late-arriving data
    
    // Performance tuning
    size_t max_pending_windows = 10;      ///< Maximum windows in queue
    size_t max_concurrent_windows = 4;    ///< Parallel window computation
    bool enable_adaptive_scheduling = true; ///< Adjust based on workload
    
    // Table names to watch
    std::string stream_s_table = "stream_s";
    std::string stream_r_table = "stream_r";
    
    // Monitoring
    bool enable_metrics = true;
    uint64_t metrics_report_interval_us = 1000000; ///< 1s metrics reporting
};

/**
 * @brief Window information
 */
struct WindowInfo {
    uint64_t window_id;           ///< Unique window identifier
    TimeRange time_range;         ///< Window time range
    int64_t watermark_us;         ///< Current watermark
    
    // State
    bool is_ready = false;        ///< Ready for computation
    bool is_computing = false;    ///< Currently computing
    bool is_completed = false;    ///< Computation completed
    bool has_late_data = false;   ///< Received late data after completion
    
    // Statistics
    size_t stream_s_count = 0;    ///< Tuples in stream S
    size_t stream_r_count = 0;    ///< Tuples in stream R
    int64_t created_at_us = 0;    ///< Window creation timestamp
    int64_t triggered_at_us = 0;  ///< Trigger timestamp
    int64_t completed_at_us = 0;  ///< Completion timestamp
};

/**
 * @brief Scheduling metrics
 */
struct SchedulingMetrics {
    // Window statistics
    uint64_t total_windows_scheduled = 0;
    uint64_t total_windows_completed = 0;
    uint64_t total_windows_failed = 0;
    uint64_t pending_windows = 0;
    uint64_t active_windows = 0;
    
    // Timing statistics
    double avg_scheduling_latency_ms = 0.0;
    double avg_window_completion_ms = 0.0;
    double max_window_completion_ms = 0.0;
    
    // Throughput
    double windows_per_second = 0.0;
    double tuples_per_second = 0.0;
    
    // Late data statistics
    uint64_t late_data_count = 0;
    uint64_t late_windows_recomputed = 0;
};

/**
 * @brief Callback for window events
 */
using WindowCallback = std::function<void(const WindowInfo&, const ComputeStatus&)>;

/**
 * @brief WindowScheduler - Automatically trigger PECJ window computations
 * 
 * Design principles:
 * - Event-driven: React to table insertions in real-time
 * - Non-blocking: Computation runs asynchronously
 * - Watermark-based: Handle out-of-order data correctly
 * - Resource-aware: Respect thread and memory limits
 * 
 * Usage flow:
 * 1. Initialize with config and resource handle
 * 2. Start scheduler (background thread)
 * 3. Watch stream tables for insertions
 * 4. Automatically trigger window computations
 * 5. Callback on window completion
 * 6. Stop scheduler gracefully
 */
class WindowScheduler {
public:
    /**
     * @brief Constructor
     * @param config Scheduler configuration
     * @param compute_engine PECJ compute engine (not owned)
     * @param table_manager Table manager for data access (not owned)
     * @param resource_handle Resource handle for task submission (not owned)
     */
    WindowScheduler(const WindowSchedulerConfig& config,
                    PECJComputeEngine* compute_engine,
                    core::TableManager* table_manager,
                    plugins::ResourceHandle* resource_handle);
    
    ~WindowScheduler();
    
    // ========== Lifecycle Management ==========
    
    /**
     * @brief Start the scheduler
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop the scheduler gracefully
     * @param wait_completion Wait for pending windows to complete
     */
    void stop(bool wait_completion = true);
    
    /**
     * @brief Check if scheduler is running
     */
    bool isRunning() const { return running_.load(); }
    
    // ========== Table Watching ==========
    
    /**
     * @brief Watch a table for insertion events
     * @param table_name Name of the table to watch
     * @param stream_id Stream identifier (0 for S, 1 for R)
     */
    void watchTable(const std::string& table_name, int stream_id);
    
    /**
     * @brief Notify scheduler of new data insertion
     * @param table_name Table that received data
     * @param timestamp Data timestamp
     * @param count Number of tuples inserted
     * 
     * This is called by StreamTable after successful insertion
     */
    void onDataInserted(const std::string& table_name, 
                       int64_t timestamp, 
                       size_t count = 1);
    
    // ========== Manual Triggering ==========
    
    /**
     * @brief Manually trigger a window computation
     * @param window_id Window identifier
     * @param time_range Time range for the window
     * @return true if window was scheduled
     */
    bool scheduleWindow(uint64_t window_id, const TimeRange& time_range);
    
    /**
     * @brief Force trigger all pending windows
     * @return Number of windows triggered
     */
    size_t triggerPendingWindows();
    
    // ========== Callback Registration ==========
    
    /**
     * @brief Register callback for window completion
     * @param callback Callback function
     */
    void onWindowCompleted(WindowCallback callback);
    
    /**
     * @brief Register callback for window failure
     * @param callback Callback function
     */
    void onWindowFailed(WindowCallback callback);
    
    // ========== Watermark Management ==========
    
    /**
     * @brief Update watermark (timestamp below which no more data expected)
     * @param watermark_us Watermark timestamp
     */
    void updateWatermark(int64_t watermark_us);
    
    /**
     * @brief Get current watermark
     */
    int64_t getWatermark() const { return watermark_us_.load(); }
    
    // ========== Query & Monitoring ==========
    
    /**
     * @brief Get scheduling metrics
     */
    SchedulingMetrics getMetrics() const;
    
    /**
     * @brief Get information about all windows
     */
    std::vector<WindowInfo> getAllWindows() const;
    
    /**
     * @brief Get information about a specific window
     */
    WindowInfo getWindowInfo(uint64_t window_id) const;
    
    /**
     * @brief Get number of pending windows
     */
    size_t getPendingWindowCount() const;
    
    /**
     * @brief Get number of active (computing) windows
     */
    size_t getActiveWindowCount() const;
    
    /**
     * @brief Reset scheduler state (clear all windows)
     */
    void reset();

private:
    // ========== Internal Methods ==========
    
    /**
     * @brief Main scheduler loop (runs in background thread)
     */
    void schedulerLoop();
    
    /**
     * @brief Check if a window should be triggered
     */
    bool shouldTriggerWindow(const WindowInfo& window);
    
    /**
     * @brief Create a new window based on timestamp
     */
    WindowInfo createWindow(int64_t timestamp);
    
    /**
     * @brief Get or create window for a timestamp
     */
    uint64_t getWindowIdForTimestamp(int64_t timestamp);
    
    /**
     * @brief Execute window computation asynchronously
     */
    void executeWindowAsync(WindowInfo& window);
    
    /**
     * @brief Update window statistics
     */
    void updateWindowStats(uint64_t window_id, const std::string& table_name, size_t count);
    
    /**
     * @brief Update watermark based on data timestamps
     */
    void updateWatermarkAuto(int64_t timestamp);
    
    /**
     * @brief Clean up old completed windows
     */
    void cleanupOldWindows();
    
    /**
     * @brief Update scheduling metrics
     */
    void updateMetrics();
    
    // ========== Member Variables ==========
    
    // Configuration
    WindowSchedulerConfig config_;
    
    // External components (not owned)
    PECJComputeEngine* compute_engine_;
    core::TableManager* table_manager_;
    plugins::ResourceHandle* resource_handle_;
    
    // Scheduler state
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::unique_ptr<std::thread> scheduler_thread_;
    
    // Window management
    std::unordered_map<uint64_t, WindowInfo> windows_;  ///< window_id -> WindowInfo
    std::priority_queue<uint64_t> pending_windows_;     ///< Windows ready to compute (min-heap by window_id)
    mutable std::mutex windows_mutex_;
    std::condition_variable windows_cv_;
    
    // Watermark tracking
    std::atomic<int64_t> watermark_us_{0};
    std::atomic<int64_t> max_timestamp_seen_{0};
    
    // Window ID generation
    std::atomic<uint64_t> next_window_id_{1};
    
    // Watched tables
    struct WatchedTable {
        std::string table_name;
        int stream_id;  // 0 for S, 1 for R
    };
    std::vector<WatchedTable> watched_tables_;
    mutable std::mutex watched_tables_mutex_;
    
    // Callbacks
    std::vector<WindowCallback> completion_callbacks_;
    std::vector<WindowCallback> failure_callbacks_;
    mutable std::mutex callbacks_mutex_;
    
    // Metrics
    mutable std::mutex metrics_mutex_;
    SchedulingMetrics metrics_;
    int64_t metrics_last_update_us_ = 0;
    
    // Performance tracking
    std::vector<double> window_completion_times_;  // For computing averages
};

} // namespace compute
} // namespace sage_tsdb

#endif // PECJ_MODE_INTEGRATED
