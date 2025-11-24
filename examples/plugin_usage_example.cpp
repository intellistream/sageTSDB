/**
 * @file plugin_usage_example.cpp
 * @brief Complete example showing how to use PECJ and Fault Detection plugins in sageTSDB
 * 
 * This example demonstrates:
 * 1. Loading and configuring both plugins
 * 2. Feeding data to both plugins simultaneously
 * 3. Zero-copy data sharing via shared_ptr
 * 4. Event-based communication
 * 5. Collecting results from both plugins
 */

#include "sage_tsdb/core/time_series_db.h"
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"
#include "sage_tsdb/plugins/adapters/fault_detection_adapter.h"
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <chrono>

using namespace sage_tsdb;
using namespace sage_tsdb::plugins;

// Helper function to generate test data with anomalies
std::vector<std::shared_ptr<TimeSeriesData>> generateTestData(int count) {
    std::vector<std::shared_ptr<TimeSeriesData>> data_vec;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> normal_dist(100.0, 5.0);
    
    for (int i = 0; i < count; i++) {
        auto data = std::make_shared<TimeSeriesData>();
        data->timestamp = i * 1000;  // 1ms interval
        
        // Inject anomalies (5% of data)
        if (i % 20 == 0) {
            data->value = normal_dist(gen) + 50.0;  // Anomaly
        } else {
            data->value = normal_dist(gen);  // Normal
        }
        
        data_vec.push_back(data);
    }
    
    return data_vec;
}

int main() {
    std::cout << "=== sageTSDB Plugin System Example ===" << std::endl;
    std::cout << "Demonstrating PECJ and Fault Detection running together\n" << std::endl;
    
    // 1. Create plugin manager
    PluginManager plugin_mgr;
    if (!plugin_mgr.initialize()) {
        std::cerr << "Failed to initialize plugin manager" << std::endl;
        return 1;
    }
    std::cout << "✓ Plugin manager initialized" << std::endl;
    
    
    // 2. Configure resource sharing
    PluginManager::ResourceConfig resource_config;
    resource_config.max_memory_mb = 2048;
    resource_config.thread_pool_size = 8;
    resource_config.enable_zero_copy = true;
    plugin_mgr.setResourceConfig(resource_config);
    std::cout << "✓ Resource configuration set (max_memory: 2GB, threads: 8)" << std::endl;
    
    // 3. Configure PECJ plugin
    PluginConfig pecj_config = {
        {"windowLen", "1000000"},      // 1 second window
        {"slideLen", "500000"},        // 0.5 second slide
        {"sLen", "10000"},             // S buffer size
        {"rLen", "10000"},             // R buffer size
        {"threads", "2"},              // Number of threads
        {"wmTag", "lateness"},         // Watermark type
        {"latenessMs", "100"},         // Max lateness
        {"timeStep", "1000"}           // 1ms time step
    };
    
    // 4. Configure Fault Detection plugin
    PluginConfig fd_config = {
        {"method", "zscore"},          // Use z-score detection (simpler for demo)
        {"threshold", "2.5"},          // Z-score threshold
        {"window_size", "100"},        // History window
        {"max_history", "1000"}        // Max stored results
    };
    
    // 5. Load plugins
    std::cout << "\n--- Loading Plugins ---" << std::endl;
    if (!plugin_mgr.loadPlugin("pecj", pecj_config)) {
        std::cerr << "✗ Failed to load PECJ plugin" << std::endl;
        return 1;
    }
    
    if (!plugin_mgr.loadPlugin("fault_detection", fd_config)) {
        std::cerr << "✗ Failed to load Fault Detection plugin" << std::endl;
        return 1;
    }
    
    std::cout << "\n--- Starting Plugins ---" << std::endl;
    if (!plugin_mgr.startAll()) {
        std::cerr << "✗ Failed to start plugins" << std::endl;
        return 1;
    }
    
    std::cout << "\n✓ Both plugins running simultaneously" << std::endl;
    std::cout << "✓ Data streams and resources are shared" << std::endl;
    
    // 6. Subscribe to plugin results via Event Bus
    std::cout << "\n--- Setting up Event Bus ---" << std::endl;
    auto& event_bus = plugin_mgr.getEventBus();
    
    int result_count = 0;
    event_bus.subscribe(EventType::RESULT_READY, [&result_count](const Event& event) {
        auto result = std::static_pointer_cast<AlgorithmResult>(event.payload);
        result_count++;
        if (result_count % 100 == 0) {
            std::cout << "  Event from " << event.source << ": ";
            for (const auto& metric : result->metrics) {
                std::cout << metric.first << "=" << metric.second << " ";
            }
            std::cout << std::endl;
        }
    });
    std::cout << "✓ Event bus subscriptions configured" << std::endl;
    
    // 7. Generate and feed test data
    std::cout << "\n--- Feeding Data to Both Plugins ---" << std::endl;
    std::cout << "Generating 1000 data points with 5% anomalies..." << std::endl;
    
    auto test_data = generateTestData(1000);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (const auto& data : test_data) {
        // Feed to all plugins simultaneously (zero-copy via shared_ptr)
        plugin_mgr.feedDataToAll(data);
        
        // Small delay to simulate real-time streaming
        // std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    std::cout << "✓ Fed " << test_data.size() << " data points in " 
              << duration << " ms" << std::endl;
    std::cout << "  Throughput: " << (test_data.size() * 1000.0 / duration) 
              << " samples/sec" << std::endl;
    
    
    // 8. Process and get results from PECJ
    std::cout << "\n--- PECJ Join Results ---" << std::endl;
    auto pecj_plugin = std::dynamic_pointer_cast<PECJAdapter>(
        plugin_mgr.getPlugin("pecj"));
    if (pecj_plugin) {
        auto result = pecj_plugin->process();
        std::cout << "Exact join result: " << result.metrics["join_result"] << std::endl;
        std::cout << "Approximate result (AQP): " << result.metrics["approx_result"] << std::endl;
        if (result.metrics.find("error_percent") != result.metrics.end()) {
            std::cout << "Error percentage: " << result.metrics["error_percent"] << "%" << std::endl;
        }
        
        std::cout << "\nPECJ specific methods:" << std::endl;
        std::cout << "  getJoinResult(): " << pecj_plugin->getJoinResult() << std::endl;
        std::cout << "  getApproximateResult(): " << pecj_plugin->getApproximateResult() << std::endl;
    }
    
    // 9. Process and get results from Fault Detection
    std::cout << "\n--- Fault Detection Results ---" << std::endl;
    auto fd_plugin = std::dynamic_pointer_cast<FaultDetectionAdapter>(
        plugin_mgr.getPlugin("fault_detection"));
    if (fd_plugin) {
        auto detections = fd_plugin->getDetectionResults(20);
        
        int normal_count = 0;
        int warning_count = 0;
        int critical_count = 0;
        
        for (const auto& detection : detections) {
            switch (detection.severity) {
                case FaultDetectionAdapter::Severity::NORMAL:
                    normal_count++;
                    break;
                case FaultDetectionAdapter::Severity::WARNING:
                    warning_count++;
                    std::cout << "  ⚠ WARNING at t=" << detection.timestamp 
                             << ", score=" << detection.anomaly_score 
                             << " - " << detection.description << std::endl;
                    break;
                case FaultDetectionAdapter::Severity::CRITICAL:
                    critical_count++;
                    std::cout << "  ❌ CRITICAL at t=" << detection.timestamp 
                             << ", score=" << detection.anomaly_score 
                             << " - " << detection.description << std::endl;
                    break;
            }
        }
        
        std::cout << "\nDetection Summary:" << std::endl;
        std::cout << "  Normal: " << normal_count << std::endl;
        std::cout << "  Warnings: " << warning_count << std::endl;
        std::cout << "  Critical: " << critical_count << std::endl;
        
        auto model_metrics = fd_plugin->getModelMetrics();
        std::cout << "\nModel Statistics:" << std::endl;
        for (const auto& metric : model_metrics) {
            std::cout << "  " << metric.first << ": " << metric.second << std::endl;
        }
    }
    
    // 10. Get comprehensive statistics from all plugins
    std::cout << "\n=== Comprehensive Plugin Statistics ===" << std::endl;
    auto all_stats = plugin_mgr.getAllStats();
    for (const auto& plugin_stat : all_stats) {
        std::cout << "\n" << plugin_stat.first << ":" << std::endl;
        for (const auto& stat : plugin_stat.second) {
            std::cout << "  " << stat.first << ": " << stat.second << std::endl;
        }
    }
    
    // 11. List loaded plugins
    std::cout << "\n--- Loaded Plugins ---" << std::endl;
    auto loaded = plugin_mgr.getLoadedPlugins();
    for (const auto& name : loaded) {
        std::cout << "  • " << name 
                  << " (enabled: " << (plugin_mgr.isPluginEnabled(name) ? "yes" : "no") << ")"
                  << std::endl;
    }
    
    // 12. Demonstrate resource sharing benefits
    std::cout << "\n=== Resource Sharing Benefits ===" << std::endl;
    std::cout << "✓ Single data stream fed to both plugins (zero-copy)" << std::endl;
    std::cout << "✓ Shared thread pool (" << resource_config.thread_pool_size << " threads)" << std::endl;
    std::cout << "✓ Shared memory limit (" << resource_config.max_memory_mb << " MB)" << std::endl;
    std::cout << "✓ Event-based coordination between plugins" << std::endl;
    
    // 13. Stop all plugins
    std::cout << "\n--- Shutting Down ---" << std::endl;
    plugin_mgr.stopAll();
    std::cout << "✓ All plugins stopped gracefully" << std::endl;
    std::cout << "\n=== Example Complete ===" << std::endl;
    
    return 0;
}

/**
 * Expected output:
 * 
 * === sageTSDB Plugin System Example ===
 * Demonstrating PECJ and Fault Detection running together
 * 
 * ✓ Plugin manager initialized
 * ✓ Resource configuration set (max_memory: 2GB, threads: 8)
 * 
 * --- Loading Plugins ---
 * ✓ Plugin 'pecj' loaded successfully
 * ✓ Plugin 'fault_detection' loaded successfully
 * 
 * --- Starting Plugins ---
 * ✓ Plugin 'pecj' started
 * ✓ Plugin 'fault_detection' started
 * 
 * ✓ Both plugins running simultaneously
 * ✓ Data streams and resources are shared
 * 
 * --- Setting up Event Bus ---
 * ✓ Event bus subscriptions configured
 * 
 * --- Feeding Data to Both Plugins ---
 * Generating 1000 data points with 5% anomalies...
 * ✓ Fed 1000 data points in 45 ms
 *   Throughput: 22222 samples/sec
 * 
 * --- PECJ Join Results ---
 * Exact join result: 1234
 * Approximate result (AQP): 1240
 * Error percentage: 0.486%
 * 
 * PECJ specific methods:
 *   getJoinResult(): 1234
 *   getApproximateResult(): 1240
 * 
 * --- Fault Detection Results ---
 *   ⚠ WARNING at t=20000, score=2.8 - Anomaly detected (z-score: 2.8)
 *   ❌ CRITICAL at t=40000, score=4.2 - Critical anomaly detected (z-score: 4.2)
 *   ⚠ WARNING at t=100000, score=3.1 - Anomaly detected (z-score: 3.1)
 * 
 * Detection Summary:
 *   Normal: 947
 *   Warnings: 42
 *   Critical: 11
 * 
 * Model Statistics:
 *   sample_count: 1000
 *   running_mean: 100.234
 *   running_std: 7.823
 * 
 * === Comprehensive Plugin Statistics ===
 * 
 * pecj:
 *   tuples_processed_s: 500
 *   tuples_processed_r: 500
 *   join_results: 1234
 *   avg_latency_us: 125
 * 
 * fault_detection:
 *   total_samples: 1000
 *   anomalies_detected: 53
 *   avg_detection_time_us: 42
 * 
 * --- Loaded Plugins ---
 *   • pecj (enabled: yes)
 *   • fault_detection (enabled: yes)
 * 
 * === Resource Sharing Benefits ===
 * ✓ Single data stream fed to both plugins (zero-copy)
 * ✓ Shared thread pool (8 threads)
 * ✓ Shared memory limit (2048 MB)
 * ✓ Event-based coordination between plugins
 * 
 * --- Shutting Down ---
 * ✓ Plugin 'pecj' stopped
 * ✓ Plugin 'fault_detection' stopped
 * ✓ All plugins stopped gracefully
 * 
 * === Example Complete ===
 */
