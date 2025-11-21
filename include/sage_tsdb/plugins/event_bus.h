#pragma once

#include "../core/time_series_data.h"
#include "plugin_interface.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sage_tsdb {
namespace plugins {

/**
 * @brief Event types for the event bus
 */
enum class EventType {
    DATA_INGESTED,      // New data arrived
    WINDOW_TRIGGERED,   // Window computation triggered
    RESULT_READY,       // Algorithm result ready
    ERROR_OCCURRED,     // Error in processing
    CUSTOM              // Custom event
};

/**
 * @brief Event structure
 */
struct Event {
    EventType type;
    int64_t timestamp;
    std::shared_ptr<void> payload;  // Generic payload
    std::string source;              // Event source plugin name
    
    Event(EventType t, int64_t ts, std::shared_ptr<void> p = nullptr, 
          const std::string& src = "")
        : type(t), timestamp(ts), payload(p), source(src) {}
};

/**
 * @brief Callback type for event subscribers
 */
using EventCallback = std::function<void(const Event&)>;

/**
 * @brief Thread-safe event bus for plugin communication
 * 
 * Implements Pub/Sub pattern for decoupled communication between:
 * - sageTSDB core
 * - PECJ plugin
 * - Fault Detection plugin
 * 
 * Features:
 * - Zero-copy data sharing via shared_ptr
 * - Async event delivery
 * - Topic-based subscription
 */
class EventBus {
public:
    EventBus() : running_(false) {}
    
    ~EventBus() {
        stop();
    }
    
    /**
     * @brief Start the event bus
     */
    void start() {
        if (running_.exchange(true)) {
            return;  // Already running
        }
        
        worker_thread_ = std::thread([this]() {
            while (running_) {
                Event event(EventType::CUSTOM, 0);
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    cv_.wait(lock, [this]() {
                        return !event_queue_.empty() || !running_;
                    });
                    
                    if (!running_) break;
                    
                    if (!event_queue_.empty()) {
                        event = event_queue_.front();
                        event_queue_.pop();
                    } else {
                        continue;
                    }
                }
                
                // Deliver event to subscribers
                deliver_event(event);
            }
        });
    }
    
    /**
     * @brief Stop the event bus
     */
    void stop() {
        if (!running_.exchange(false)) {
            return;  // Already stopped
        }
        
        cv_.notify_all();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    /**
     * @brief Subscribe to events of a specific type
     */
    int subscribe(EventType type, EventCallback callback) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        int id = next_subscriber_id_++;
        subscribers_[type].push_back({id, callback});
        return id;
    }
    
    /**
     * @brief Unsubscribe from events
     */
    void unsubscribe(int subscriber_id) {
        std::lock_guard<std::mutex> lock(subscribers_mutex_);
        for (auto& pair : subscribers_) {
            auto& subs = pair.second;
            subs.erase(
                std::remove_if(subs.begin(), subs.end(),
                    [subscriber_id](const Subscriber& s) {
                        return s.id == subscriber_id;
                    }),
                subs.end());
        }
    }
    
    /**
     * @brief Publish an event
     */
    void publish(const Event& event) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            event_queue_.push(event);
        }
        cv_.notify_one();
    }
    
    /**
     * @brief Publish event with time series data (zero-copy)
     */
    void publish_data(const std::shared_ptr<TimeSeriesData>& data, 
                     const std::string& source = "") {
        Event event(EventType::DATA_INGESTED, data->timestamp, 
                   std::static_pointer_cast<void>(data), source);
        publish(event);
    }
    
    /**
     * @brief Publish algorithm result
     */
    void publish_result(const std::shared_ptr<AlgorithmResult>& result,
                       const std::string& source = "") {
        Event event(EventType::RESULT_READY, result->timestamp,
                   std::static_pointer_cast<void>(result), source);
        publish(event);
    }

private:
    struct Subscriber {
        int id;
        EventCallback callback;
    };
    
    void deliver_event(const Event& event) {
        std::vector<EventCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(subscribers_mutex_);
            auto it = subscribers_.find(event.type);
            if (it != subscribers_.end()) {
                for (const auto& sub : it->second) {
                    callbacks.push_back(sub.callback);
                }
            }
        }
        
        // Deliver outside lock to avoid deadlock
        for (const auto& callback : callbacks) {
            try {
                callback(event);
            } catch (...) {
                // Log error but continue
            }
        }
    }
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
    
    std::queue<Event> event_queue_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    
    std::unordered_map<EventType, std::vector<Subscriber>> subscribers_;
    std::mutex subscribers_mutex_;
    int next_subscriber_id_ = 0;
};

} // namespace plugins
} // namespace sage_tsdb
