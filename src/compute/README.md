# PECJ Compute Engine - Deep Integration Mode

## 概述

`PECJComputeEngine` 是 PECJ 在 sageTSDB 中的深度融合实现，采用**无状态计算引擎**设计模式。

## 设计原则

1. **数据中心化**：所有数据存储在 sageTSDB 表中，PECJ 不持有数据
2. **无状态计算**：PECJ 作为纯函数计算引擎，不维护内部状态
3. **资源托管**：线程、内存、GPU 由 sageTSDB ResourceManager 统一管理
4. **可观测性**：所有计算状态和结果可通过 sageTSDB 查询

## 文件结构

```
compute/
├── README.md                    # 本文件
├── pecj_compute_engine.h        # 计算引擎接口定义
├── pecj_compute_engine.cpp      # 计算引擎实现
├── window_scheduler.h           # 窗口调度器（待实现）
├── window_scheduler.cpp
├── compute_state_manager.h      # 状态管理器（待实现）
└── compute_state_manager.cpp
```

## 使用方法

### 1. 初始化

```cpp
#include "sage_tsdb/compute/pecj_compute_engine.h"

using namespace sage_tsdb::compute;

// 创建配置
ComputeConfig config;
config.window_len_us = 1000000;      // 1秒窗口
config.slide_len_us = 500000;        // 500ms滑动
config.operator_type = "IAWJ";       // 使用IAWJ算法
config.max_memory_bytes = 2ULL << 30; // 2GB内存限制

// 获取资源句柄
auto resource_handle = db->getResourceManager()->allocateForCompute(
    "pecj_engine", 
    ResourceRequest{.requested_threads = 4}
);

// 初始化引擎
PECJComputeEngine engine;
if (!engine.initialize(config, db, resource_handle.get())) {
    // 初始化失败处理
}
```

### 2. 执行窗口计算

```cpp
// 定义时间窗口
TimeRange window{
    .start_us = 1000000,  // 1秒
    .end_us = 2000000     // 2秒
};

// 执行计算（同步调用）
auto status = engine.executeWindowJoin(window_id, window);

if (status.success) {
    std::cout << "Join results: " << status.join_count << std::endl;
    std::cout << "Computation time: " << status.computation_time_ms << "ms" << std::endl;
} else {
    std::cerr << "Computation failed: " << status.error << std::endl;
}
```

### 3. 查询结果

```cpp
// 从结果表查询
auto results = db->query("join_results", QueryConfig{
    .time_range = window,
    .filter_tags = {{"window_id", std::to_string(window_id)}}
});

for (const auto& result : results) {
    // 处理结果
}
```

### 4. 监控指标

```cpp
// 获取运行时指标
auto metrics = engine.getMetrics();

std::cout << "Total windows: " << metrics.total_windows_completed << std::endl;
std::cout << "Avg latency: " << metrics.avg_window_latency_ms << "ms" << std::endl;
std::cout << "P99 latency: " << metrics.p99_window_latency_ms << "ms" << std::endl;
std::cout << "Peak memory: " << metrics.peak_memory_bytes / 1024 / 1024 << "MB" << std::endl;
```

## 数据流

```
外部数据源
    ↓
db->insert("stream_s", data)
    ↓
sageTSDB MemTable/LSM-Tree
    ↓
WindowScheduler 检测窗口完整
    ↓
engine.executeWindowJoin(window_id, range)
    ├─ db->query("stream_s", range)
    ├─ db->query("stream_r", range)
    ├─ PECJ 核心算法执行
    └─ db->insert("join_results", results)
    ↓
下游应用 db->query("join_results")
```

## 配置选项

### ComputeConfig

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `window_len_us` | uint64_t | 1000000 | 窗口长度（微秒） |
| `slide_len_us` | uint64_t | 500000 | 滑动长度（微秒） |
| `operator_type` | string | "IAWJ" | 算法类型（IAWJ/MWAY） |
| `max_delay_us` | uint64_t | 100000 | 最大延迟（微秒） |
| `aqp_threshold` | double | 0.05 | AQP误差阈值 |
| `max_memory_bytes` | size_t | 2GB | 内存限制 |
| `max_threads` | int | 4 | 线程数限制 |
| `enable_aqp` | bool | true | 启用AQP降级 |
| `timeout_ms` | uint64_t | 1000 | 计算超时（毫秒） |

## 性能优化

### 1. 批量查询

```cpp
// 优化前：逐条查询
for (auto ts : timestamps) {
    auto data = db->query("stream_s", TimeRange{ts, ts+1});
}

// 优化后：批量查询
auto data = db->query("stream_s", TimeRange{start, end});
```

### 2. 内存预分配

```cpp
config.max_memory_bytes = estimated_window_size * 2;  // 预留2倍空间
```

### 3. 异步执行

```cpp
resource_handle->submitTask([&engine, window_id, range]() {
    auto status = engine.executeWindowJoin(window_id, range);
    // 处理结果
});
```

## 故障处理

### 超时降级到AQP

```cpp
if (status.timeout_occurred && status.used_aqp) {
    std::cout << "Used AQP estimation: " << status.aqp_estimate << std::endl;
}
```

### 内存超限

```cpp
if (!status.success && status.error.find("Memory") != std::string::npos) {
    // 减少窗口大小或增加内存配额
    config.window_len_us /= 2;
    engine.reset();
    engine.initialize(config, db, resource_handle.get());
}
```

### 重试机制

```cpp
const int MAX_RETRIES = 3;
for (int retry = 0; retry < MAX_RETRIES; ++retry) {
    auto status = engine.executeWindowJoin(window_id, range);
    if (status.success) break;
    
    // 指数退避
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << retry)));
}
```

## 编译配置

### CMakeLists.txt

```cmake
# 启用深度融合模式
set(PECJ_MODE "INTEGRATED" CACHE STRING "PECJ integration mode")

if(PECJ_MODE STREQUAL "INTEGRATED")
    add_definitions(-DPECJ_MODE_INTEGRATED)
    
    # 构建 PECJ 计算引擎
    add_library(sage_tsdb_pecj_engine
        src/compute/pecj_compute_engine.cpp
    )
    
    target_link_libraries(sage_tsdb_pecj_engine
        PRIVATE PECJ::core
        PRIVATE sage_tsdb_core
    )
endif()
```

### 构建命令

```bash
cd sageTSDB/build
cmake .. \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR=/path/to/PECJ
make -j8
```

## 测试

### 单元测试

```cpp
TEST(PECJComputeEngineTest, BasicJoin) {
    PECJComputeEngine engine;
    
    // Mock DB and ResourceHandle
    auto status = engine.executeWindowJoin(1, TimeRange{0, 1000000});
    
    EXPECT_TRUE(status.success);
    EXPECT_GT(status.join_count, 0);
}
```

### 集成测试

```bash
cd sageTSDB/build
./test/compute_engine_integration_test
```

## 调试

### 启用详细日志

```cpp
// 在初始化前设置日志级别
db->setLogLevel(LogLevel::DEBUG);
```

### 查看中间状态

```cpp
// 查询输入数据
auto s_data = db->query("stream_s", window_range);
auto r_data = db->query("stream_r", window_range);

std::cout << "Stream S: " << s_data.size() << " tuples" << std::endl;
std::cout << "Stream R: " << r_data.size() << " tuples" << std::endl;
```

## 常见问题

### Q: 为什么结果为空？

A: 检查：
1. 输入表是否有数据：`db->query("stream_s", range)`
2. 时间范围是否正确：`range.valid()`
3. Join条件是否匹配

### Q: 内存占用过高？

A: 尝试：
1. 减小窗口大小
2. 增加 `max_memory_bytes`
3. 启用AQP降级

### Q: 延迟过高？

A: 优化：
1. 减少窗口大小
2. 增加线程数
3. 启用SIMD优化

## 参考文档

- [设计文档](../../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [PECJ原始论文](../../docs/PECJ_paper.pdf)
- [sageTSDB API文档](../../docs/API.md)
