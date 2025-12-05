# sageTSDB 深度融合 PECJ 集成设计文档

版本: v3.0 (Deep Integration Architecture)
更新日期: 2024-12-04
分支: `pecj_deep_integration`

## 摘要

本文档描述将 PECJ 作为**纯计算引擎**深度融合到 sageTSDB 的方案。核心原则：

1. **数据存储优先**：所有数据必须先写入 sageTSDB 的 Table（MemTable/LSM-Tree）
2. **资源完全托管**：线程、内存、GPU、状态管理完全由 sageTSDB 控制
3. **PECJ 无状态化**：PECJ 仅作为计算函数，不维护自己的数据缓冲区和生命周期
4. **统一查询接口**：通过 sageTSDB 的 Query API 获取数据，而非直接 feed 数据到 PECJ

这是一种**database-centric**设计，而非传统的插件模式。

## 设计目标

- **数据中心化**：sageTSDB 是唯一的数据真相来源（Single Source of Truth）
- **计算无状态化**：PECJ 作为纯函数计算引擎，不持有数据状态
- **资源统一管理**：所有系统资源（线程、内存、GPU）由 sageTSDB ResourceManager 分配
- **状态可观测**：PECJ 的执行状态、中间结果全部由 sageTSDB 管理和持久化
- **可测试性**：PECJ 的计算逻辑可独立测试，不依赖复杂的数据管道

## 架构原则

### 双模式设计（共存以支持性能对比实验）

本设计支持两种模式**同时存在**，通过预编译参数选择：

#### 模式 1：传统插件模式（Baseline）
```
外部数据 → PluginManager → PECJ内部缓冲区 → PECJ计算 → 结果
                ↓
            sageTSDB存储（可选）
```
**特点**：
- PECJ 维护独立的数据缓冲区（data_queue_）
- PECJ 自行创建和管理线程
- 数据异步 feedData() 方式输入
- 适合：独立运行、快速原型验证

**编译参数**：`-DPECJ_MODE=PLUGIN`

#### 模式 2：深度融合模式（Integrated）
```
外部数据 → sageTSDB Table (MemTable/LSM) → PECJ Compute Engine → 结果写回Table
            ↑                                      ↓
            └──────────── ResourceManager ────────┘
                    (线程/内存/GPU/状态)
```
**特点**：
- 数据单一存储在 sageTSDB 表中
- PECJ 无状态纯计算引擎
- 资源由 ResourceManager 统一管理
- 适合：生产环境、大规模部署

**编译参数**：`-DPECJ_MODE=INTEGRATED`

### 模式对比表

| 维度 | 插件模式 (Baseline) | 深度融合模式 (Integrated) |
|------|-------------------|------------------------|
| **数据存储** | PECJ 缓冲区 + sageTSDB | 仅 sageTSDB 表 |
| **数据输入** | `feedData()` 异步 | `db->insert()` 同步 |
| **计算触发** | PECJ 内部轮询 | WindowScheduler 调度 |
| **线程管理** | PECJ 自建线程 | ResourceManager 线程池 |
| **内存占用** | 2x（重复存储） | 1x（单一存储） |
| **资源控制** | 无全局配额 | 精确配额限制 |
| **状态管理** | PECJ 内部状态 | sageTSDB 表持久化 |
| **适用场景** | 原型验证、实验对比 | 生产环境、长期运行 |
| **测试隔离** | 需要完整 sageTSDB | 可独立测试计算逻辑 |

## 关键设计决策

### 为什么需要双模式？

| 需求 | 插件模式的优势 | 深度融合模式的优势 |
|------|--------------|------------------|
| **实验对比** | 提供性能基准（Baseline） | 验证优化效果 |
| **灵活性** | 快速迭代、独立调试 | 生产级稳定性 |
| **兼容性** | 兼容现有 PECJ 代码 | 深度集成 sageTSDB 特性 |
| **部署场景** | 边缘设备、单机部署 | 数据中心、集群部署 |
| **开发测试** | 无需完整 sageTSDB 环境 | 集成测试更真实 |

### 双模式实验对比的价值

**对比维度**：
1. **吞吐量**：每秒处理的事件数 (events/sec)
2. **延迟**：端到端延迟分布 (P50, P99, P999)
3. **内存占用**：峰值内存和平均内存 (MB)
4. **CPU 利用率**：线程数和 CPU 使用率 (%)
5. **可扩展性**：增加流数量时的性能衰减
6. **故障恢复**：重启后恢复时间 (ms)

**实验参数**：
- 数据规模：1M, 10M, 100M 事件
- 窗口大小：1s, 5s, 10s
- 乱序程度：0%, 10%, 30%
- 并发流数：2, 4, 8

### 核心约束

1. **PECJ 不持有数据**
   - 所有输入数据从 sageTSDB 表查询
   - 计算中间状态也存储在表中（而非内存）
   - 结果立即写回表

2. **PECJ 不创建线程**
   - 使用 `ResourceHandle::submitTask()` 提交任务
   - sageTSDB 的线程池执行任务
   - 线程数严格受 `ResourceRequest::requested_threads` 限制

3. **PECJ 不管理生命周期**
   - 由 sageTSDB 的 `ComputeScheduler` 调度执行
   - 失败重试由 sageTSDB 控制
   - 没有独立的 start/stop 语义

## 核心组件设计

### 1. 数据流架构

```
┌─────────────────────────────────────────────────────────────┐
│                        sageTSDB Core                         │
├─────────────────────────────────────────────────────────────┤
│  外部数据                                                     │
│     ↓                                                        │
│  [MemTable] → [Immutable MemTable] → [LSM-Tree Level 0-N]  │
│     ↓            ↓                      ↓                    │
│     └────────────┴──────────────────────┘                   │
│                  ↓                                           │
│          [TimeSeriesDB Query API]                           │
│                  ↓                                           │
│     ┌────────────┴─────────────┐                           │
│     ↓                           ↓                            │
│ [Stream S Table]          [Stream R Table]                  │
│     ↓                           ↓                            │
│     └────────────┬──────────────┘                           │
│                  ↓                                           │
│         [PECJ Compute Engine]                               │
│           (无状态计算函数)                                     │
│                  ↓                                           │
│         [Join Result Table]                                 │
│                  ↓                                           │
│         [EventBus 通知下游]                                  │
└─────────────────────────────────────────────────────────────┘
```

#### 关键约束：
1. **数据必须先入表**：外部数据先写入 `StreamSTable` 或 `StreamRTable`
2. **PECJ 从表读取**：通过 `TimeSeriesDB::query()` 获取窗口内数据
3. **结果写回表**：Join 结果写入 `JoinResultTable`，不直接返回给调用方

### 2. PECJ 计算引擎接口（新设计）

PECJ 不再是一个有状态的插件，而是一个**纯函数计算引擎**：

```cpp
namespace sage_tsdb {
namespace compute {

/**
 * @brief PECJ 计算引擎（无状态）
 * 
 * 设计原则：
 * - 不持有任何数据缓冲区
 * - 不创建线程（使用 ResourceHandle 提交任务）
 * - 不管理生命周期（由 sageTSDB 调度）
 * - 计算结果写回 sageTSDB 表
 */
class PECJComputeEngine {
public:
    /**
     * @brief 初始化计算引擎（一次性配置）
     * @param config 算法参数（窗口大小、延迟阈值等）
     * @param db 指向 TimeSeriesDB 的引用（用于读写数据）
     * @param resource_handle 资源句柄（用于提交计算任务）
     */
    bool initialize(const ComputeConfig& config,
                   TimeSeriesDB* db,
                   ResourceHandle* resource_handle);
    
    /**
     * @brief 执行窗口 Join 计算（同步调用）
     * @param window_id 窗口标识符
     * @param time_range 时间范围 [start, end)
     * @return 计算状态（成功/失败/部分完成）
     * 
     * 执行流程：
     * 1. 从 db 查询 StreamS 和 StreamR 在 time_range 内的数据
     * 2. 调用 PECJ 核心算法执行 Join
     * 3. 将结果写入 db 的 JoinResultTable
     * 4. 返回计算统计信息
     */
    ComputeStatus executeWindowJoin(uint64_t window_id,
                                    const TimeRange& time_range);
    
    /**
     * @brief 获取计算统计（资源使用、延迟等）
     */
    ComputeMetrics getMetrics() const;
    
    /**
     * @brief 重置计算状态（清除缓存、重置计数器）
     */
    void reset();

private:
    TimeSeriesDB* db_;                        // 数据库引用（不拥有）
    ResourceHandle* resource_handle_;         // 资源句柄（不拥有）
    ComputeConfig config_;                    // 算法配置
    
    // PECJ 核心算法对象（轻量级，无状态）
    std::unique_ptr<OoOJoin::AbstractOperator> pecj_operator_;
};

} // namespace compute
} // namespace sage_tsdb
```

### 3. sageTSDB 表设计

#### 3.1 Stream 表（输入）
```cpp
// Stream S 和 Stream R 各自独立的表
class StreamTable {
public:
    // 标准 sageTSDB 表接口
    size_t insert(const TimeSeriesData& data);
    std::vector<TimeSeriesData> query(const TimeRange& range, 
                                      const Tags& filter_tags = {});
    void createIndex(const std::string& field_name);
    
    // 窗口语义支持
    std::vector<TimeSeriesData> queryWindow(uint64_t window_id);
};

// sageTSDB 中注册两个表
db->createTable("stream_s", StreamTable);
db->createTable("stream_r", StreamTable);
```

#### 3.2 Join 结果表（输出）
```cpp
class JoinResultTable {
public:
    // 存储 Join 结果的结构
    struct JoinRecord {
        uint64_t window_id;
        int64_t timestamp;
        size_t join_count;        // 精确 Join 结果数
        double aqp_estimate;      // AQP 估计值
        std::vector<uint8_t> payload;  // 序列化的详细结果
    };
    
    size_t insertJoinResult(const JoinRecord& record);
    std::vector<JoinRecord> queryByWindow(uint64_t window_id);
};
```

### 4. ResourceManager 深度集成

```cpp
class ResourceManager {
public:
    /**
     * @brief 为 PECJ 分配计算资源
     * @param compute_name 计算引擎名称（如 "pecj_engine"）
     * @param request 资源请求
     * @return 资源句柄
     */
    std::shared_ptr<ResourceHandle> allocateForCompute(
        const std::string& compute_name,
        const ResourceRequest& request);
    
    /**
     * @brief 监控 PECJ 资源使用
     */
    ResourceUsage getComputeUsage(const std::string& compute_name) const;
    
    /**
     * @brief 强制限流（当资源超限时）
     */
    void throttleCompute(const std::string& compute_name, double factor);
};
```

### 5. 状态管理（由 sageTSDB 负责）

```cpp
class ComputeStateManager {
public:
    /**
     * @brief 保存 PECJ 中间状态到表
     * @param compute_name 计算引擎名称
     * @param state 状态数据（序列化后）
     */
    bool saveState(const std::string& compute_name, 
                  const std::vector<uint8_t>& state);
    
    /**
     * @brief 恢复 PECJ 状态
     */
    std::vector<uint8_t> loadState(const std::string& compute_name);
    
    /**
     * @brief 持久化到磁盘（通过 LSM-Tree）
     */
    bool persistState(const std::string& compute_name);
};
```

## 使用示例

### 场景：实时股票交易流 Join

```cpp
// 1. 初始化 sageTSDB
TimeSeriesDB db;
db.enablePersistence("/data/sage_tsdb");

// 2. 创建 Stream 表
db.createTable("stock_orders", TableType::TimeSeries);
db.createTable("stock_trades", TableType::TimeSeries);
db.createTable("join_results", TableType::TimeSeries);

// 3. 初始化 PECJ 计算引擎
ComputeConfig pecj_config;
pecj_config.window_len_us = 1000000;  // 1秒窗口
pecj_config.slide_len_us = 500000;    // 500ms 滑动
pecj_config.operator_type = OperatorType::IAWJ;

ResourceRequest resource_req;
resource_req.requested_threads = 4;
resource_req.max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 2GB

auto resource_handle = db.getResourceManager()->allocateForCompute(
    "pecj_engine", resource_req);

PECJComputeEngine pecj_engine;
pecj_engine.initialize(pecj_config, &db, resource_handle.get());

// 4. 数据写入（由外部数据源驱动）
void onOrderData(const TimeSeriesData& data) {
    db.insert("stock_orders", data);
}

void onTradeData(const TimeSeriesData& data) {
    db.insert("stock_trades", data);
}

// 5. 定期触发窗口计算（由 sageTSDB 的 WindowScheduler 驱动）
void onWindowTrigger(uint64_t window_id, const TimeRange& range) {
    // 异步提交计算任务（不阻塞数据写入）
    resource_handle->submitTask([&, window_id, range]() {
        auto status = pecj_engine.executeWindowJoin(window_id, range);
        
        if (status.success) {
            // 结果已写入 join_results 表
            LOG_INFO("Window {} completed: {} joins", 
                     window_id, status.join_count);
            
            // 通知下游应用（可选）
            db.getEventBus()->publish("window_completed", status);
        }
    });
}

// 6. 查询 Join 结果
auto results = db.query("join_results", QueryConfig{
    .time_range = {now - 3600000, now},  // 最近1小时
    .filter_tags = {{"symbol", "AAPL"}}
});

// 7. 监控资源使用
auto metrics = db.getResourceManager()->getComputeUsage("pecj_engine");
LOG_INFO("PECJ: threads={}, memory={}MB, latency={}ms",
         metrics.threads_used,
         metrics.memory_used_bytes / 1024 / 1024,
         metrics.avg_latency_ms);
```

## 实施路线（分阶段迁移）

### Phase 1: 基础设施准备（1-2周）
1. **扩展 TimeSeriesDB 接口**
   - 添加 `createTable(name, type)` API
   - 支持多表独立存储（stream_s, stream_r, join_results）
   - 实现表级别的 query/insert

2. **实现 ComputeStateManager**
   - 状态序列化/反序列化
   - 通过 LSM-Tree 持久化

3. **扩展 ResourceManager**
   - 添加 `allocateForCompute()` 接口
   - 实现计算任务的资源隔离

### Phase 2: PECJ 无状态化改造（2-3周）
1. **重构 PECJAdapter → PECJComputeEngine**
   - 移除内部数据队列（`data_queue_`）
   - 移除工作线程（`worker_thread_`）
   - 仅保留核心算法调用

2. **实现数据适配层**
   ```cpp
   // 从 sageTSDB 格式转换为 PECJ TrackTuple
   std::vector<TrackTuple> convertFromTable(
       const std::vector<TimeSeriesData>& db_data);
   
   // 从 PECJ 结果转换为 sageTSDB 格式
   std::vector<TimeSeriesData> convertToTable(
       const JoinResult& pecj_result);
   ```

3. **集成测试**
   - 单元测试：验证计算正确性（不依赖 sageTSDB）
   - 集成测试：验证 sageTSDB 表读写一致性

### Phase 3: 窗口调度集成（1-2周）
1. **实现 WindowScheduler**
   ```cpp
   class WindowScheduler {
   public:
       // 基于时间或数据量触发窗口计算
       void scheduleWindow(uint64_t window_id, const TimeRange& range);
       
       // 监听表的插入事件，自动触发窗口
       void watchTable(const std::string& table_name);
   };
   ```

2. **事件驱动集成**
   - 数据插入 → 触发窗口检查
   - 窗口完整 → 提交 PECJ 计算任务
   - 计算完成 → 发布 EventBus 通知

### Phase 4: 性能优化与监控（1周）
1. **批量查询优化**
   - 预取窗口数据到 MemTable
   - SIMD 加速数据转换

2. **资源监控完善**
   - 实时监控 PECJ 计算延迟
   - 自动降级（超时切换到 AQP 模式）

3. **持久化策略**
   - 定期 checkpoint PECJ 状态
   - 支持故障恢复

## 迁移对比

| 维度 | 旧插件模式 | 新深度融合模式 |
|------|-----------|---------------|
| **数据存储** | PECJ 内部缓冲区 + sageTSDB | 仅 sageTSDB Table |
| **资源管理** | PECJ 自行创建线程 | sageTSDB ResourceManager |
| **状态管理** | PECJ 内部状态 | sageTSDB ComputeStateManager |
| **接口模式** | 异步 feedData() | 同步 executeWindowJoin() |
| **测试隔离** | 需要完整 sageTSDB | 可独立测试计算逻辑 |
| **内存占用** | 双份数据（PECJ + DB） | 单份数据 |
| **故障恢复** | 复杂（需恢复 PECJ 状态） | 简单（仅恢复表数据） |

## 验证要点（验收准则）

### 功能验证
- [ ] 数据写入 stream_s/stream_r 表后可查询
- [ ] PECJ 能从表中正确读取窗口数据
- [ ] Join 结果正确写入 join_results 表
- [ ] WindowScheduler 能自动触发窗口计算
- [ ] 支持多窗口并发计算（资源隔离）

### 性能验证
- [ ] 吞吐量不低于旧模式的 90%
- [ ] 端到端延迟（数据写入 → Join 完成）<100ms (P99)
- [ ] 内存占用减少 30%+（消除重复缓冲）
- [ ] 线程数精确控制在配额内

### 可靠性验证
- [ ] 支持 PECJ 计算失败后重试（从表重新读取）
- [ ] 支持 sageTSDB 重启后状态恢复
- [ ] 资源超限时自动降级（AQP 模式）
- [ ] 监控指标完整（延迟、吞吐、错误率）

## CMake 构建配置（支持双模式）

### 预编译参数

```cmake
# ========== PECJ 集成总开关 ==========
option(SAGE_TSDB_ENABLE_PECJ "Enable PECJ integration" OFF)

# ========== PECJ 运行模式选择 ==========
set(PECJ_MODE "PLUGIN" CACHE STRING "PECJ integration mode: PLUGIN or INTEGRATED")
set_property(CACHE PECJ_MODE PROPERTY STRINGS "PLUGIN" "INTEGRATED")

# ========== PECJ 源码路径 ==========
set(PECJ_DIR "" CACHE PATH "Path to PECJ source or build directory")

if(SAGE_TSDB_ENABLE_PECJ)
    message(STATUS "PECJ Integration Enabled: Mode=${PECJ_MODE}")
    
    # 查找 PECJ 库
    find_package(PECJ REQUIRED PATHS ${PECJ_DIR})
    
    # 根据模式定义不同的编译宏
    if(PECJ_MODE STREQUAL "PLUGIN")
        add_definitions(-DPECJ_MODE_PLUGIN)
        message(STATUS "  - Using PLUGIN mode (traditional adapter)")
        
        # 构建传统插件模式
        add_library(sage_tsdb_pecj_plugin
            src/plugins/adapters/pecj_adapter.cpp
            src/plugins/adapters/pecj_plugin_impl.cpp
        )
        target_link_libraries(sage_tsdb_pecj_plugin 
            PRIVATE PECJ::core
            PRIVATE sage_tsdb_core
        )
        
    elseif(PECJ_MODE STREQUAL "INTEGRATED")
        add_definitions(-DPECJ_MODE_INTEGRATED)
        message(STATUS "  - Using INTEGRATED mode (deep fusion)")
        
        # 构建深度融合模式
        add_library(sage_tsdb_pecj_engine
            src/compute/pecj_compute_engine.cpp
            src/compute/pecj_data_adapter.cpp
            src/compute/window_scheduler.cpp
            src/compute/compute_state_manager.cpp
        )
        target_link_libraries(sage_tsdb_pecj_engine 
            PRIVATE PECJ::core
            PRIVATE sage_tsdb_core
        )
        
    else()
        message(FATAL_ERROR "Invalid PECJ_MODE: ${PECJ_MODE}. Must be PLUGIN or INTEGRATED")
    endif()
    
    # 通用编译宏（两种模式都需要）
    add_definitions(-DPECJ_FULL_INTEGRATION)
    
else()
    message(STATUS "PECJ Integration Disabled - using stub implementation")
    add_definitions(-DPECJ_STUB_MODE)
endif()
```

### 构建示例

#### 示例 1：构建插件模式（用于性能基准）
```bash
cd sageTSDB/build
cmake .. \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=PLUGIN \
    -DPECJ_DIR=/path/to/PECJ
make -j8
```

#### 示例 2：构建深度融合模式（用于生产）
```bash
cd sageTSDB/build
cmake .. \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR=/path/to/PECJ
make -j8
```

#### 示例 3：构建两种模式用于对比实验
```bash
# 构建插件模式
mkdir -p build_plugin && cd build_plugin
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=PLUGIN -DPECJ_DIR=/path/to/PECJ
make -j8

# 构建深度融合模式
cd ..
mkdir -p build_integrated && cd build_integrated
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED -DPECJ_DIR=/path/to/PECJ
make -j8

# 运行性能对比
./build_plugin/bin/pecj_benchmark > results_plugin.txt
./build_integrated/bin/pecj_benchmark > results_integrated.txt
python scripts/compare_performance.py results_plugin.txt results_integrated.txt
```

### 代码中的条件编译

```cpp
// ========== include/sage_tsdb/pecj_integration.h ==========
#pragma once

#ifdef PECJ_MODE_PLUGIN
    // 传统插件模式头文件
    #include "plugins/adapters/pecj_adapter.h"
    using PECJInterface = sage_tsdb::plugins::PECJAdapter;
    
#elif defined(PECJ_MODE_INTEGRATED)
    // 深度融合模式头文件
    #include "compute/pecj_compute_engine.h"
    using PECJInterface = sage_tsdb::compute::PECJComputeEngine;
    
#else
    // Stub 模式（无 PECJ）
    #include "plugins/adapters/pecj_stub.h"
    using PECJInterface = sage_tsdb::plugins::PECJStub;
#endif

// ========== 使用统一接口 ==========
class TimeSeriesDB {
public:
    void setupPECJ(const Config& config) {
        #ifdef PECJ_MODE_PLUGIN
            // 插件模式初始化
            pecj_ = std::make_unique<PECJAdapter>(config);
            pecj_->initialize(config);
            pecj_->start();
            
        #elif defined(PECJ_MODE_INTEGRATED)
            // 深度融合模式初始化
            auto handle = resource_manager_->allocateForCompute("pecj", resource_req);
            pecj_ = std::make_unique<PECJComputeEngine>();
            pecj_->initialize(config, this, handle.get());
            
        #else
            // Stub 模式
            pecj_ = std::make_unique<PECJStub>();
        #endif
    }
    
private:
    std::unique_ptr<PECJInterface> pecj_;
};
```

## 性能对比实验指南

### 实验环境准备

```bash
# 1. 构建两种模式
./scripts/build_dual_mode.sh

# 2. 生成测试数据集
./scripts/generate_test_data.sh \
    --events 10000000 \
    --out-of-order-ratio 0.2 \
    --streams 2

# 3. 运行性能测试
./scripts/run_benchmark.sh \
    --mode both \
    --config configs/pecj_benchmark.json \
    --output results/
```

### 实验配置模板

```json
{
  "experiment": {
    "name": "PECJ Plugin vs Integrated Performance Comparison",
    "runs": 5,
    "warmup_runs": 2
  },
  "workload": {
    "total_events": 10000000,
    "window_size_ms": 1000,
    "slide_size_ms": 500,
    "out_of_order_ratio": 0.2,
    "key_range": 1000
  },
  "modes": [
    {
      "name": "plugin",
      "config": {
        "threads": 4,
        "buffer_size": 100000
      }
    },
    {
      "name": "integrated",
      "config": {
        "threads": 4,
        "max_memory_mb": 2048
      }
    }
  ],
  "metrics": [
    "throughput_events_per_sec",
    "latency_p50_ms",
    "latency_p99_ms",
    "memory_peak_mb",
    "memory_avg_mb",
    "cpu_usage_percent"
  ]
}
```

### 性能指标收集

```cpp
// ========== src/benchmark/performance_collector.h ==========
struct PerformanceMetrics {
    // 模式标识
    std::string mode;  // "plugin" or "integrated"
    
    // 吞吐量指标
    uint64_t total_events;
    double duration_seconds;
    double throughput_events_per_sec;
    
    // 延迟指标 (microseconds)
    double latency_min;
    double latency_max;
    double latency_avg;
    double latency_p50;
    double latency_p95;
    double latency_p99;
    double latency_p999;
    
    // 资源指标
    size_t memory_peak_bytes;
    size_t memory_avg_bytes;
    int threads_used;
    double cpu_usage_percent;
    
    // PECJ 特定指标
    uint64_t windows_completed;
    uint64_t join_results_count;
    double avg_window_latency_ms;
    
    // 模式差异
    bool has_data_duplication;  // Plugin: true, Integrated: false
    bool uses_internal_threads; // Plugin: true, Integrated: false
};

// 对比报告生成
void generateComparisonReport(
    const PerformanceMetrics& plugin_metrics,
    const PerformanceMetrics& integrated_metrics) {
    
    std::cout << "=== PECJ Performance Comparison ===" << std::endl;
    std::cout << "Throughput: " << std::endl;
    std::cout << "  Plugin:     " << plugin_metrics.throughput_events_per_sec << " events/s" << std::endl;
    std::cout << "  Integrated: " << integrated_metrics.throughput_events_per_sec << " events/s" << std::endl;
    std::cout << "  Speedup:    " << (integrated_metrics.throughput_events_per_sec / plugin_metrics.throughput_events_per_sec) << "x" << std::endl;
    
    // ... 更多对比输出
}
```

### 可视化对比脚本

```python
# scripts/compare_performance.py
import json
import matplotlib.pyplot as plt

def plot_comparison(plugin_results, integrated_results):
    metrics = ['throughput', 'latency_p99', 'memory_peak', 'cpu_usage']
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    
    # 吞吐量对比
    axes[0, 0].bar(['Plugin', 'Integrated'], 
                   [plugin_results['throughput'], integrated_results['throughput']])
    axes[0, 0].set_title('Throughput (events/sec)')
    
    # 延迟对比
    axes[0, 1].bar(['Plugin', 'Integrated'],
                   [plugin_results['latency_p99'], integrated_results['latency_p99']])
    axes[0, 1].set_title('P99 Latency (ms)')
    
    # 内存对比
    axes[1, 0].bar(['Plugin', 'Integrated'],
                   [plugin_results['memory_peak'], integrated_results['memory_peak']])
    axes[1, 0].set_title('Peak Memory (MB)')
    
    # CPU 对比
    axes[1, 1].bar(['Plugin', 'Integrated'],
                   [plugin_results['cpu_usage'], integrated_results['cpu_usage']])
    axes[1, 1].set_title('CPU Usage (%)')
    
    plt.savefig('pecj_performance_comparison.png')
```

## 后续扩展

1. **多计算引擎支持**
   - 实现统一的 `IComputeEngine` 接口
   - 支持注册多个计算引擎（PECJ、Fault Detection、Anomaly Detection）

2. **分布式计算**
   - 通过 sageTSDB 的分布式表（sharding）支持大规模计算
   - PECJ 计算任务可调度到多节点

3. **GPU 加速**
   - ResourceManager 自动分配 GPU 设备
   - PECJ 计算在 GPU 上执行（需 PECJ 支持）

4. **自动模式切换**
   - 运行时根据负载动态切换模式
   - 插件模式 ↔ 深度融合模式热切换

## 常见问题 (FAQ)

### Q1: 数据从外部到 PECJ 计算的完整流程？
```
1. 外部数据源 → TimeSeriesDB::insert("stream_s", data)
2. sageTSDB 写入 MemTable，必要时 flush 到 LSM-Tree
3. WindowScheduler 检测窗口完整 → 触发计算
4. PECJComputeEngine::executeWindowJoin() 执行：
   a. 从 stream_s/stream_r 表查询窗口数据
   b. 调用 PECJ 核心算法
   c. 将结果写入 join_results 表
5. EventBus 发布 "window_completed" 事件
6. 下游应用通过 query("join_results") 获取结果
```

### Q2: PECJ 的状态（如 watermark、窗口进度）如何管理？
所有状态都序列化后存储在 sageTSDB 的特殊表 `_compute_state` 中：
```cpp
// 保存状态
db.insert("_compute_state", {
    .tags = {{"compute_name", "pecj_engine"}},
    .fields = {{"watermark", current_watermark},
               {"window_id", current_window_id}},
    .payload = serialized_operator_state
});

// 恢复状态
auto state_data = db.query("_compute_state", {
    .filter_tags = {{"compute_name", "pecj_engine"}}
});
pecj_engine.restoreState(state_data[0].payload);
```

### Q3: 如何保证 PECJ 计算的实时性？
1. **优先级调度**：高优先级窗口任务优先执行
2. **增量计算**：每个窗口独立计算，不阻塞后续窗口
3. **超时降级**：超过阈值自动切换到 AQP 模式（牺牲精度换速度）
4. **预取优化**：WindowScheduler 提前将数据加载到 MemTable

### Q4: 如何处理乱序数据（out-of-order）？
sageTSDB 的表支持乱序插入：
```cpp
// 即使时间戳乱序，也能正确插入
db.insert("stream_s", {.timestamp = t1, ...});
db.insert("stream_s", {.timestamp = t0, ...});  // t0 < t1

// PECJ 查询时会自动按时间排序
auto data = db.query("stream_s", time_range);  // 已排序
```

### Q5: 如何在插件模式和深度融合模式之间切换？
**双模式共存**，通过 CMake 参数选择：

```bash
# 使用插件模式（传统方式）
cmake -DPECJ_MODE=PLUGIN ..

# 使用深度融合模式
cmake -DPECJ_MODE=INTEGRATED ..
```

**运行时行为差异**：
- **插件模式**：使用 `feedData()` 异步输入，PECJ 内部管理缓冲区和线程
- **深度融合模式**：数据先 `db->insert()`，通过 WindowScheduler 触发计算

**代码兼容性**：
```cpp
#ifdef PECJ_MODE_PLUGIN
    // 插件模式代码
    pecj_adapter->feedStreamS(data);
#elif defined(PECJ_MODE_INTEGRATED)
    // 深度融合模式代码
    db->insert("stream_s", data);
#endif
```

### Q6: 如何调试 PECJ 计算问题？
sageTSDB 提供完整的调试能力：
```cpp
// 1. 查询输入数据
auto s_data = db.query("stream_s", window_time_range);
auto r_data = db.query("stream_r", window_time_range);

// 2. 手动触发计算（跳过 WindowScheduler）
auto status = pecj_engine.executeWindowJoin(window_id, time_range);

// 3. 检查中间状态
auto state = db.query("_compute_state", {
    .filter_tags = {{"compute_name", "pecj_engine"}}
});

// 4. 验证输出
auto results = db.query("join_results", {
    .filter_tags = {{"window_id", std::to_string(window_id)}}
});
```

### Q7: 多个 PECJ 实例如何隔离？
通过 ResourceManager 的资源配额隔离：
```cpp
// 为不同查询分配独立的计算引擎
auto pecj_query1 = db.createComputeEngine("pecj_q1", {
    .requested_threads = 2,
    .max_memory_bytes = 1GB
});

auto pecj_query2 = db.createComputeEngine("pecj_q2", {
    .requested_threads = 2,
    .max_memory_bytes = 1GB
});

// 资源总和不超过系统配额
```

## 实现注意事项

### 1. 线程安全
- `PECJComputeEngine` 的所有公共方法必须线程安全
- 多个窗口可能并发调用 `executeWindowJoin()`
- 使用 `std::shared_mutex` 保护共享状态

### 2. 内存管理
```cpp
class PECJComputeEngine {
private:
    // 使用 Arena 分配器减少碎片
    std::unique_ptr<MemoryArena> arena_;
    
    // 定期检查内存使用
    void checkMemoryUsage() {
        if (arena_->usage() > config_.max_memory_bytes) {
            // 触发清理或降级
            cleanupOldWindows();
        }
    }
};
```

### 3. 错误处理
```cpp
ComputeStatus PECJComputeEngine::executeWindowJoin(...) {
    try {
        // 查询数据
        auto s_data = db_->query("stream_s", time_range);
        auto r_data = db_->query("stream_r", time_range);
        
        // 执行计算
        auto result = pecj_operator_->join(s_data, r_data);
        
        // 写回结果
        db_->insert("join_results", result);
        
        return {.success = true, .join_count = result.size()};
        
    } catch (const std::exception& e) {
        // 记录错误，不抛出异常
        LOG_ERROR("PECJ computation failed: {}", e.what());
        return {.success = false, .error = e.what()};
    }
}
```

### 4. 性能优化建议
- **批量查询**：一次查询整个窗口，而非逐条查询
- **零拷贝**：使用 `std::shared_ptr` 在 sageTSDB 和 PECJ 间共享数据
- **异步写入**：计算结果异步写入表，不阻塞下一个窗口
- **索引优化**：在时间戳字段建立索引加速窗口查询

### 5. 监控指标
必须上报的关键指标：
```cpp
struct PECJMetrics {
    // 吞吐量
    uint64_t windows_completed;
    uint64_t tuples_processed;
    
    // 延迟
    double avg_window_latency_ms;
    double p99_window_latency_ms;
    
    // 资源
    size_t peak_memory_bytes;
    int active_threads;
    
    // 质量
    double avg_join_selectivity;  // join_count / (|S| * |R|)
    double aqp_error_rate;        // |exact - aqp| / exact
    
    // 错误
    uint64_t failed_windows;
    uint64_t retries;
};
```

---
**文档状态**: v3.0 深度融合架构设计，已废弃 v2.0 的插件模式。  
**实施跟踪**: 参见 issue #TBD  
**代码仓库**: `pecj_deep_integration` 分支  
**审阅者**: 需要 PECJ 团队和 sageTSDB 团队共同审阅  
**预期完成**: Phase 1-3 约 6-8 周，Phase 4 约 1-2 周
