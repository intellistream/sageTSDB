/**
 * @file window_scheduler.cpp
 * @brief Implementation of WindowScheduler for PECJ Deep Integration
 * 
 * @version v3.0 (Deep Integration)
 * @date 2024-12-05
 */

#include "sage_tsdb/compute/window_scheduler.h"

#ifdef PECJ_MODE_INTEGRATED

#include "sage_tsdb/core/table_manager.h"
#include "sage_tsdb/core/stream_table.h"
#include "sage_tsdb/plugins/resource_manager.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace sage_tsdb {
namespace compute {

// ========== Helper Functions ==========

static int64_t getCurrentTimeUs() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

// ========== Constructor & Destructor ==========

WindowScheduler::WindowScheduler(const WindowSchedulerConfig& config,
                                 PECJComputeEngine* compute_engine,
                                 core::TableManager* table_manager,
                                 plugins::ResourceHandle* resource_handle)
    : config_(config),
      compute_engine_(compute_engine),
      table_manager_(table_manager),
      resource_handle_(resource_handle) {
    
    if (!compute_engine_ || !table_manager_ || !resource_handle_) {
        throw std::invalid_argument("WindowScheduler: null pointer arguments");
    }
}

WindowScheduler::~WindowScheduler() {
    stop(false);  // Don't wait for completion
}

// ========== Lifecycle Management ==========

bool WindowScheduler::start() {
    if (running_.load()) {
        std::cerr << "WindowScheduler: already running" << std::endl;
        return false;
    }
    
    running_.store(true);
    stop_requested_.store(false);
    
    // Start scheduler thread
    scheduler_thread_ = std::make_unique<std::thread>(&WindowScheduler::schedulerLoop, this);
    
    std::cout << "WindowScheduler: started (window=" << config_.window_len_us 
              << "us, slide=" << config_.slide_len_us << "us)" << std::endl;
    
    return true;
}

void WindowScheduler::stop(bool wait_completion) {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "WindowScheduler: stopping..." << std::endl;
    
    stop_requested_.store(true);
    windows_cv_.notify_all();
    
    if (scheduler_thread_ && scheduler_thread_->joinable()) {
        scheduler_thread_->join();
    }
    
    if (wait_completion) {
        // Wait for all active windows to complete
        std::unique_lock<std::mutex> lock(windows_mutex_);
        // Simple wait - in production, add timeout
        while (getActiveWindowCount() > 0) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            lock.lock();
        }
    }
    
    running_.store(false);
    std::cout << "WindowScheduler: stopped" << std::endl;
}

// ========== Table Watching ==========

void WindowScheduler::watchTable(const std::string& table_name, int stream_id) {
    std::lock_guard<std::mutex> lock(watched_tables_mutex_);
    watched_tables_.push_back({table_name, stream_id});
    std::cout << "WindowScheduler: watching table '" << table_name 
              << "' (stream_id=" << stream_id << ")" << std::endl;
}

void WindowScheduler::onDataInserted(const std::string& table_name, 
                                     int64_t timestamp, 
                                     size_t count) {
    if (!running_.load()) {
        return;
    }
    
    // Update watermark
    updateWatermarkAuto(timestamp);
    
    // Get or create window for this timestamp
    uint64_t window_id = getWindowIdForTimestamp(timestamp);
    
    // Update window statistics
    updateWindowStats(window_id, table_name, count);
    
    // Check if window should be triggered
    {
        std::lock_guard<std::mutex> lock(windows_mutex_);
        auto it = windows_.find(window_id);
        if (it != windows_.end() && shouldTriggerWindow(it->second)) {
            if (!it->second.is_computing && !it->second.is_completed) {
                it->second.is_ready = true;
                pending_windows_.push(window_id);
                windows_cv_.notify_one();
            }
        }
    }
}

// ========== Manual Triggering ==========

bool WindowScheduler::scheduleWindow(uint64_t window_id, const TimeRange& time_range) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    auto it = windows_.find(window_id);
    if (it == windows_.end()) {
        // Create new window
        WindowInfo window;
        window.window_id = window_id;
        window.time_range = time_range;
        window.watermark_us = watermark_us_.load();
        window.created_at_us = getCurrentTimeUs();
        window.is_ready = true;
        
        windows_[window_id] = window;
        pending_windows_.push(window_id);
        windows_cv_.notify_one();
        
        return true;
    } else if (!it->second.is_computing && !it->second.is_completed) {
        // Window exists but not started
        it->second.is_ready = true;
        pending_windows_.push(window_id);
        windows_cv_.notify_one();
        
        return true;
    }
    
    return false;
}

size_t WindowScheduler::triggerPendingWindows() {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    size_t count = 0;
    for (auto& pair : windows_) {
        auto& window = pair.second;
        if (!window.is_ready && !window.is_computing && !window.is_completed) {
            window.is_ready = true;
            pending_windows_.push(window.window_id);
            count++;
        }
    }
    
    if (count > 0) {
        windows_cv_.notify_all();
    }
    
    return count;
}

// ========== Callback Registration ==========

void WindowScheduler::onWindowCompleted(WindowCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    completion_callbacks_.push_back(callback);
}

void WindowScheduler::onWindowFailed(WindowCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    failure_callbacks_.push_back(callback);
}

// ========== Watermark Management ==========

void WindowScheduler::updateWatermark(int64_t watermark_us) {
    int64_t old_watermark = watermark_us_.load();
    if (watermark_us > old_watermark) {
        watermark_us_.store(watermark_us);
    }
}

// ========== Query & Monitoring ==========

SchedulingMetrics WindowScheduler::getMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

std::vector<WindowInfo> WindowScheduler::getAllWindows() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    std::vector<WindowInfo> result;
    result.reserve(windows_.size());
    
    for (const auto& pair : windows_) {
        result.push_back(pair.second);
    }
    
    return result;
}

WindowInfo WindowScheduler::getWindowInfo(uint64_t window_id) const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    auto it = windows_.find(window_id);
    if (it != windows_.end()) {
        return it->second;
    }
    
    return WindowInfo{};  // Empty window info
}

size_t WindowScheduler::getPendingWindowCount() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    return pending_windows_.size();
}

size_t WindowScheduler::getActiveWindowCount() const {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    size_t count = 0;
    for (const auto& pair : windows_) {
        if (pair.second.is_computing) {
            count++;
        }
    }
    
    return count;
}

void WindowScheduler::reset() {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    windows_.clear();
    while (!pending_windows_.empty()) {
        pending_windows_.pop();
    }
    next_window_id_.store(1);
    watermark_us_.store(0);
    max_timestamp_seen_.store(0);
    
    std::lock_guard<std::mutex> metrics_lock(metrics_mutex_);
    metrics_ = SchedulingMetrics{};
}

// ========== Internal Methods ==========

void WindowScheduler::schedulerLoop() {
    while (!stop_requested_.load()) {
        try {
            std::unique_lock<std::mutex> lock(windows_mutex_);
            
            // Wait for pending windows or timeout
            windows_cv_.wait_for(lock, 
                std::chrono::microseconds(config_.trigger_interval_us),
                [this]() { 
                    return !pending_windows_.empty() || stop_requested_.load(); 
                });
            
            if (stop_requested_.load()) {
                break;
            }
            
            // Process pending windows
            while (!pending_windows_.empty() && 
                   getActiveWindowCount() < config_.max_concurrent_windows) {
                
                uint64_t window_id = pending_windows_.top();
                pending_windows_.pop();
                
                auto it = windows_.find(window_id);
                if (it == windows_.end() || it->second.is_computing || it->second.is_completed) {
                    continue;  // Skip invalid or already processed windows
                }
                
                // Execute window asynchronously
                executeWindowAsync(it->second);
            }
            
            lock.unlock();
            
            // Periodic maintenance
            cleanupOldWindows();
            updateMetrics();
            
        } catch (const std::exception& e) {
            std::cerr << "WindowScheduler: error in scheduler loop: " << e.what() << std::endl;
        }
    }
}

bool WindowScheduler::shouldTriggerWindow(const WindowInfo& window) {
    // Check if window is already computing or completed
    if (window.is_computing || window.is_completed) {
        return false;
    }
    
    int64_t current_watermark = watermark_us_.load();
    
    switch (config_.trigger_policy) {
        case TriggerPolicy::TimeBased:
            // Trigger when watermark passes window end + slack
            return current_watermark >= (window.time_range.end_us + config_.watermark_slack_us);
        
        case TriggerPolicy::CountBased:
            // Trigger when enough tuples collected
            return (window.stream_s_count + window.stream_r_count) >= config_.trigger_count_threshold;
        
        case TriggerPolicy::Hybrid:
            // Trigger when either condition is met
            return current_watermark >= (window.time_range.end_us + config_.watermark_slack_us) ||
                   (window.stream_s_count + window.stream_r_count) >= config_.trigger_count_threshold;
        
        case TriggerPolicy::Manual:
            // Only trigger manually
            return false;
    }
    
    return false;
}

WindowInfo WindowScheduler::createWindow(int64_t timestamp) {
    WindowInfo window;
    window.window_id = next_window_id_.fetch_add(1);
    window.created_at_us = getCurrentTimeUs();
    window.watermark_us = watermark_us_.load();
    
    // Calculate window boundaries based on window type
    switch (config_.window_type) {
        case WindowType::Tumbling: {
            // Align to window boundaries
            int64_t window_start = (timestamp / config_.window_len_us) * config_.window_len_us;
            window.time_range.start_us = window_start;
            window.time_range.end_us = window_start + config_.window_len_us;
            break;
        }
        
        case WindowType::Sliding: {
            // Create overlapping windows
            int64_t window_start = (timestamp / config_.slide_len_us) * config_.slide_len_us;
            window.time_range.start_us = window_start;
            window.time_range.end_us = window_start + config_.window_len_us;
            break;
        }
        
        case WindowType::Session: {
            // Session windows (gap-based) - simplified implementation
            window.time_range.start_us = timestamp;
            window.time_range.end_us = timestamp + config_.window_len_us;
            break;
        }
    }
    
    return window;
}

uint64_t WindowScheduler::getWindowIdForTimestamp(int64_t timestamp) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    // Find existing window that contains this timestamp
    for (const auto& pair : windows_) {
        if (pair.second.time_range.contains(timestamp)) {
            return pair.first;
        }
    }
    
    // Create new window
    WindowInfo window = createWindow(timestamp);
    uint64_t window_id = window.window_id;
    windows_[window_id] = window;
    
    return window_id;
}

void WindowScheduler::executeWindowAsync(WindowInfo& window) {
    window.is_computing = true;
    window.triggered_at_us = getCurrentTimeUs();
    
    // Submit to resource handle (asynchronous execution)
    if (resource_handle_) {
        resource_handle_->submitTask([this, window_id = window.window_id]() {
            WindowInfo window_copy;
            
            {
                std::lock_guard<std::mutex> lock(windows_mutex_);
                auto it = windows_.find(window_id);
                if (it == windows_.end()) {
                    return;  // Window was removed
                }
                window_copy = it->second;
            }
            
            // Execute PECJ computation
            auto start_time = getCurrentTimeUs();
            ComputeStatus status = compute_engine_->executeWindowJoin(
                window_copy.window_id, 
                window_copy.time_range
            );
            auto end_time = getCurrentTimeUs();
            
            double completion_time_ms = (end_time - start_time) / 1000.0;
            
            // Update window state
            {
                std::lock_guard<std::mutex> lock(windows_mutex_);
                auto it = windows_.find(window_id);
                if (it != windows_.end()) {
                    it->second.is_computing = false;
                    it->second.is_completed = true;
                    it->second.completed_at_us = end_time;
                    window_copy = it->second;  // Get updated copy
                }
            }
            
            // Update metrics
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                if (status.success) {
                    metrics_.total_windows_completed++;
                } else {
                    metrics_.total_windows_failed++;
                }
                
                window_completion_times_.push_back(completion_time_ms);
                if (window_completion_times_.size() > 100) {
                    window_completion_times_.erase(window_completion_times_.begin());
                }
            }
            
            // Invoke callbacks
            {
                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                if (status.success) {
                    for (auto& callback : completion_callbacks_) {
                        try {
                            callback(window_copy, status);
                        } catch (const std::exception& e) {
                            std::cerr << "WindowScheduler: callback error: " << e.what() << std::endl;
                        }
                    }
                } else {
                    for (auto& callback : failure_callbacks_) {
                        try {
                            callback(window_copy, status);
                        } catch (const std::exception& e) {
                            std::cerr << "WindowScheduler: callback error: " << e.what() << std::endl;
                        }
                    }
                }
            }
        });
    }
}

void WindowScheduler::updateWindowStats(uint64_t window_id, 
                                        const std::string& table_name, 
                                        size_t count) {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    auto it = windows_.find(window_id);
    if (it == windows_.end()) {
        return;
    }
    
    // Determine which stream this table belongs to
    if (table_name == config_.stream_s_table) {
        it->second.stream_s_count += count;
    } else if (table_name == config_.stream_r_table) {
        it->second.stream_r_count += count;
    }
}

void WindowScheduler::updateWatermarkAuto(int64_t timestamp) {
    // Update max timestamp seen
    int64_t old_max = max_timestamp_seen_.load();
    while (timestamp > old_max) {
        if (max_timestamp_seen_.compare_exchange_weak(old_max, timestamp)) {
            break;
        }
    }
    
    // Update watermark (timestamp - max_delay)
    int64_t new_watermark = timestamp - config_.max_delay_us;
    updateWatermark(new_watermark);
}

void WindowScheduler::cleanupOldWindows() {
    std::lock_guard<std::mutex> lock(windows_mutex_);
    
    int64_t current_time = getCurrentTimeUs();
    int64_t cleanup_threshold = current_time - (10 * config_.window_len_us);  // Keep 10 windows worth
    
    std::vector<uint64_t> to_remove;
    for (const auto& pair : windows_) {
        if (pair.second.is_completed && 
            pair.second.completed_at_us < cleanup_threshold) {
            to_remove.push_back(pair.first);
        }
    }
    
    for (uint64_t window_id : to_remove) {
        windows_.erase(window_id);
    }
}

void WindowScheduler::updateMetrics() {
    int64_t current_time = getCurrentTimeUs();
    
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    
    // Update only if enough time has passed
    if (current_time - metrics_last_update_us_ < config_.metrics_report_interval_us) {
        return;
    }
    
    // Calculate average completion time
    if (!window_completion_times_.empty()) {
        double sum = 0.0;
        double max_time = 0.0;
        for (double time : window_completion_times_) {
            sum += time;
            max_time = std::max(max_time, time);
        }
        metrics_.avg_window_completion_ms = sum / window_completion_times_.size();
        metrics_.max_window_completion_ms = max_time;
    }
    
    // Calculate windows per second
    double elapsed_s = (current_time - metrics_last_update_us_) / 1000000.0;
    if (elapsed_s > 0) {
        metrics_.windows_per_second = metrics_.total_windows_completed / elapsed_s;
    }
    
    // Update current state
    metrics_.pending_windows = pending_windows_.size();
    metrics_.active_windows = getActiveWindowCount();
    
    metrics_last_update_us_ = current_time;
}

} // namespace compute
} // namespace sage_tsdb

#endif // PECJ_MODE_INTEGRATED
