# ResourceManager 使用指南

## 概述

ResourceManager 是 sageTSDB 的核心资源调度器，负责统一管理所有插件和计算引擎的线程、内存和 GPU 资源。

**注意**: ResourceManager 已从 `plugins/` 迁移到 `core/` 目录 (v3.2, 2024-12-14)。

## 快速开始

### 1. 创建 ResourceManager 实例

```cpp
#include "sage_tsdb/core/resource_manager.h"

using namespace sage_tsdb::core;

// 创建实例
auto rm = createResourceManager();

// 设置全局限制（可选）
rm->setGlobalLimits(
    16,                        // 最大线程数
    4ULL * 1024 * 1024 * 1024  // 4GB 内存限制
);
```

### 2. 为插件分配资源

```cpp
// 定义资源请求
ResourceRequest req;
req.requested_threads = 4;           // 请求4个工作线程
req.max_memory_bytes = 512 * 1024 * 1024;  // 512MB 内存限制
req.priority = 10;                   // 高优先级

// 分配资源
auto handle = rm->allocate("my_plugin", req);
if (!handle || !handle->isValid()) {
    // 资源分配失败
    return;
}
```

### 3. 提交任务到线程池

```cpp
// 提交异步任务
handle->submitTask([]() {
    // 你的计算逻辑
    std::cout << "Task executed in managed thread pool" << std::endl;
});

// 提交多个任务
for (int i = 0; i < 100; ++i) {
    handle->submitTask([i]() {
        processData(i);
    });
}
```

### 4. 上报资源使用情况

```cpp
// 定期上报（建议每1-5秒）
ResourceUsage usage;
usage.threads_used = 4;
usage.memory_used_bytes = getCurrentMemoryUsage();
usage.queue_length = getQueueSize();
usage.tuples_processed = total_processed_count;
usage.avg_latency_ms = computeAvgLatency();

handle->reportUsage(usage);
```

### 5. 查询资源使用

```cpp
// 查询单个插件
auto usage = rm->queryUsage("my_plugin");
std::cout << "Memory used: " << usage.memory_used_bytes << " bytes" << std::endl;

// 查询全局使用
auto total = rm->getTotalUsage();
std::cout << "Total threads: " << total.threads_used << std::endl;

// 检查资源压力
if (rm->isUnderPressure()) {
    std::cout << "Warning: System under resource pressure!" << std::endl;
}
```

### 6. 释放资源

```cpp
// 显式释放
rm->release("my_plugin");

// 或者让 handle 自动析构（推荐）
// handle.reset(); 或超出作用域
```

## 完整示例：集成到插件

```cpp
class MyPlugin : public IAlgorithmPlugin {
public:
    bool initialize(const PluginConfig& config) override {
        // 从 PluginManager 获取 ResourceManager
        auto rm = getResourceManager(); // 假设由框架提供
        
        // 请求资源
        ResourceRequest req;
        req.requested_threads = 
            std::stoi(config.at("threads"));
        req.max_memory_bytes = 
            std::stoull(config.at("max_memory_mb")) * 1024 * 1024;
        
        handle_ = rm->allocate("my_plugin", req);
        if (!handle_ || !handle_->isValid()) {
            return false;
        }
        
        // 启动监控线程
        monitor_thread_ = std::thread([this]() {
            monitorResourceUsage();
        });
        
        return true;
    }
    
    void processData(const TimeSeriesData& data) override {
        // 提交到托管线程池，而非自己创建线程
        handle_->submitTask([this, data]() {
            doHeavyComputation(data);
            
            // 更新统计
            processed_count_.fetch_add(1);
        });
    }
    
private:
    void monitorResourceUsage() {
        while (running_) {
            ResourceUsage usage;
            usage.threads_used = 4; // 实际使用的线程
            usage.memory_used_bytes = estimateMemoryUsage();
            usage.tuples_processed = processed_count_.load();
            usage.avg_latency_ms = latency_tracker_.getAverage();
            
            handle_->reportUsage(usage);
            
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
    
    std::shared_ptr<ResourceHandle> handle_;
    std::thread monitor_thread_;
    std::atomic<uint64_t> processed_count_{0};
    bool running_ = true;
};
```

## 最佳实践

### 1. 任务粒度
- ✅ 提交中等大小的任务（10ms - 1s）
- ❌ 避免提交过小任务（微秒级）导致调度开销
- ❌ 避免提交阻塞式长任务（>10s）

### 2. 资源请求
```cpp
// 合理的资源请求
ResourceRequest req;
req.requested_threads = 4;  // 不要超过硬件线程数
req.max_memory_bytes = 512 * 1024 * 1024;  // 预留足够余量
req.priority = 0;  // 大多数情况使用默认优先级
```

### 3. 错误处理
```cpp
handle->submitTask([this]() {
    try {
        riskyOperation();
    } catch (const std::exception& e) {
        // 记录错误并上报
        ResourceUsage usage;
        usage.errors_count = error_count_.fetch_add(1);
        usage.last_error = e.what();
        handle_->reportUsage(usage);
    }
});
```

### 4. 优雅关闭
```cpp
void shutdown() {
    running_ = false;
    
    // 等待监控线程
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    // 释放资源（handle 析构时自动完成）
    handle_.reset();
}
```

## 降级策略

当系统检测到资源压力时（`isUnderPressure() == true`），应实施降级：

```cpp
if (rm->isUnderPressure()) {
    // 策略1: 减少工作负载
    reduceProcessingRate();
    
    // 策略2: 切换到轻量模式
    switchToLightweightMode();
    
    // 策略3: 调整资源配额
    ResourceRequest reduced = current_request;
    reduced.requested_threads /= 2;
    rm->adjustQuota("my_plugin", reduced);
}
```

## 监控集成

与 Prometheus 集成示例：

```cpp
void exportMetrics() {
    auto usage = rm->queryUsage("my_plugin");
    
    prometheus::Gauge& mem_gauge = /* ... */;
    mem_gauge.Set(usage.memory_used_bytes);
    
    prometheus::Counter& tuples_counter = /* ... */;
    tuples_counter.Increment(usage.tuples_processed);
    
    prometheus::Histogram& latency_hist = /* ... */;
    latency_hist.Observe(usage.avg_latency_ms);
}
```

## 故障排查

### 问题1：任务未执行
```cpp
// 检查 handle 是否有效
if (!handle->isValid()) {
    // 可能原因：资源已释放或超额
}

// 检查任务是否成功入队
bool ok = handle->submitTask(task);
if (!ok) {
    // 队列可能已满或 handle 失效
}
```

### 问题2：内存泄漏
```cpp
// 确保上报实际内存使用
usage.memory_used_bytes = getCurrentRSS(); // 而非预估值
```

### 问题3：线程数不符预期
```cpp
// 查看实际分配
auto allocated = handle->getAllocated();
std::cout << "Allocated threads: " 
          << allocated.requested_threads << std::endl;
// 可能与请求不同（受全局限制影响）
```

## 参考

- [设计文档](../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [单元测试](../tests/test_resource_manager.cpp)
- [API 文档](../include/sage_tsdb/core/resource_manager.h)
