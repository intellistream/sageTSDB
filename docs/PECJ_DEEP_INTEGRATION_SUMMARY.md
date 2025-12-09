# PECJ 深度融合架构总结

> 快速参考：PECJ 与 sageTSDB 深度融合的核心变更

## 核心变更一览

### 双模式架构（共存用于性能对比）

#### 模式 1：插件模式（传统，作为 Baseline）
```
外部数据 → Plugin Manager
              ↓
        PECJ Adapter
        ├─ 内部队列 (data_queue_)
        ├─ 工作线程 (worker_thread_)
        ├─ PECJ 算子 (维护窗口状态)
        └─ 结果发布 → EventBus
              ↓
        sageTSDB 存储（可选）
```
**编译参数**：`-DPECJ_MODE=PLUGIN`
**用途**：性能基准测试、快速原型验证

#### 模式 2：深度融合（新设计，生产推荐）
```
外部数据 → sageTSDB Core
              ↓
        MemTable / LSM-Tree
        ├─ stream_s 表
        ├─ stream_r 表
        └─ join_results 表
              ↓
        Window Scheduler（触发计算）
              ↓
        PECJ Compute Engine
        ├─ 从表查询数据
        ├─ 纯函数计算
        └─ 结果写回表
              ↓
        ResourceManager（统一资源管理）
```
**编译参数**：`-DPECJ_MODE=INTEGRATED`
**用途**：生产环境、大规模部署、资源可控场景

### 为什么需要双模式共存？

1. **性能对比实验**：量化深度融合模式的优化效果
2. **灵活部署**：不同场景选择最优模式
3. **向后兼容**：保留插件模式用于已有集成
4. **风险控制**：新模式有问题时可快速回退

## 三大核心原则（深度融合模式）

### 1️⃣ 数据存储优先（Database-First）

**旧模式**：
```cpp
// 数据先进 PECJ，后进 DB
pecj_adapter->feedData(data);  // PECJ 内部缓冲
db->insert(data);              // 可能延迟或不存储
```

**新模式**：
```cpp
// 数据必须先进 DB
db->insert("stream_s", data);  // 唯一入口

// PECJ 从 DB 读取
auto window_data = db->query("stream_s", time_range);
pecj_engine.compute(window_data);
```

### 2️⃣ 资源完全托管（Resource-Managed）

| 资源类型 | 旧模式 | 新模式 |
|---------|-------|--------|
| **线程** | PECJ 自行创建 | ResourceManager 统一分配 |
| **内存** | PECJ 内部缓冲区无限增长 | sageTSDB 表统一管理 |
| **GPU** | PECJ 自行初始化 | ResourceManager 分配设备 |
| **状态** | PECJ 内部状态 | sageTSDB 持久化表 |

### 3️⃣ PECJ 无状态化（Stateless Compute）

**旧模式**：PECJ 是有状态的算子
```cpp
class PECJAdapter {
    std::queue<Data> data_queue_;      // ❌ 持有数据
    std::thread worker_thread_;        // ❌ 自管线程
    WindowState window_state_;         // ❌ 内部状态
};
```

**新模式**：PECJ 是纯计算函数
```cpp
class PECJComputeEngine {
    // ✅ 不持有数据，从 DB 查询
    // ✅ 不创建线程，用 ResourceHandle
    // ✅ 不保存状态，写回 DB
    
    ComputeStatus compute(const InputData& input) {
        // 纯函数：相同输入 → 相同输出
        return result;
    }
};
```

## 关键接口变更

### 数据输入接口

```cpp
// 旧接口（已废弃）
void feedData(const TimeSeriesData& data);
void feedStreamS(const TimeSeriesData& data);
void feedStreamR(const TimeSeriesData& data);

// 新接口
// 统一通过 TimeSeriesDB 插入
db->insert("stream_s", data);
db->insert("stream_r", data);
```

### 计算触发接口

```cpp
// 旧接口（主动轮询）
AlgorithmResult process();  // PECJ 内部决定何时计算

// 新接口（被动调度）
ComputeStatus executeWindowJoin(
    uint64_t window_id,
    const TimeRange& time_range
);  // 由 WindowScheduler 触发
```

### 结果获取接口

```cpp
// 旧接口（内存返回）
size_t getJoinResult();  // 从 PECJ 内存获取

// 新接口（表查询）
auto results = db->query("join_results", {
    .filter_tags = {{"window_id", "123"}}
});
```

## 数据流转完整示例

```cpp
// ========== 1. 初始化阶段 ==========
TimeSeriesDB db;
db.createTable("stream_s", TableType::TimeSeries);
db.createTable("stream_r", TableType::TimeSeries);
db.createTable("join_results", TableType::TimeSeries);

ResourceRequest req{.requested_threads = 4, .max_memory_bytes = 2GB};
auto handle = db.getResourceManager()->allocateForCompute("pecj", req);

PECJComputeEngine pecj;
pecj.initialize(config, &db, handle.get());

// ========== 2. 数据写入阶段 ==========
void onExternalData(const TimeSeriesData& data, bool is_stream_s) {
    if (is_stream_s) {
        db.insert("stream_s", data);  // ✅ 先入 DB
    } else {
        db.insert("stream_r", data);
    }
    // ❌ 不再直接调用 pecj_adapter->feedData()
}

// ========== 3. 窗口触发阶段 ==========
void onWindowComplete(uint64_t window_id, TimeRange range) {
    // 由 WindowScheduler 自动触发
    handle->submitTask([&, window_id, range]() {
        // 在 sageTSDB 线程池中执行
        auto status = pecj.executeWindowJoin(window_id, range);
        
        if (status.success) {
            LOG_INFO("Window {} computed: {} joins", 
                     window_id, status.join_count);
        }
    });
}

// ========== 4. 结果查询阶段 ==========
void queryResults(uint64_t start_time, uint64_t end_time) {
    auto results = db.query("join_results", {
        .time_range = {start_time, end_time}
    });
    
    for (const auto& row : results) {
        auto window_id = row.tags["window_id"];
        auto join_count = row.fields["join_count"];
        // 处理结果...
    }
}
```

## 构建配置（双模式）

### 编译参数

| 参数 | 值 | 说明 |
|-----|---|------|
| `SAGE_TSDB_ENABLE_PECJ` | ON/OFF | 是否启用 PECJ 集成（默认 OFF） |
| `PECJ_MODE` | PLUGIN/INTEGRATED | 选择运行模式（默认 PLUGIN） |
| `PECJ_DIR` | /path/to/PECJ | PECJ 源码路径 |

### 构建示例

```bash
# 构建插件模式（传统方式，用于基准测试）
mkdir build_plugin && cd build_plugin
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=PLUGIN -DPECJ_DIR=/path/to/PECJ
make -j8

# 构建深度融合模式（新方式，生产推荐）
mkdir build_integrated && cd build_integrated
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED -DPECJ_DIR=/path/to/PECJ
make -j8

# 运行性能对比实验
./build_plugin/bin/pecj_benchmark --config test.json --output results_plugin.csv
./build_integrated/bin/pecj_benchmark --config test.json --output results_integrated.csv
python3 scripts/compare_results.py results_plugin.csv results_integrated.csv
```

### 代码中的条件编译

```cpp
#ifdef PECJ_MODE_PLUGIN
    // 插件模式代码
    #include "plugins/adapters/pecj_adapter.h"
    auto pecj = std::make_unique<PECJAdapter>(config);
    pecj->feedData(data);
    
#elif defined(PECJ_MODE_INTEGRATED)
    // 深度融合模式代码
    #include "compute/pecj_compute_engine.h"
    db->insert("stream_s", data);
    auto pecj = std::make_unique<PECJComputeEngine>();
    pecj->executeWindowJoin(window_id, time_range);
#endif
```

## 实施清单

### 插件模式（保留）
- [x] `PECJAdapter` 类保持不变
- [x] 支持 `feedData()` 异步输入
- [x] 内部数据队列和工作线程
- [ ] 添加性能监控埋点（用于对比）

### 深度融合模式（新增）
- [ ] `PECJComputeEngine` 类（无状态计算引擎）
- [ ] `WindowScheduler` （窗口触发调度器）
- [ ] `ComputeStateManager` （状态持久化到表）
- [ ] 数据转换层（TimeSeriesData ↔ TrackTuple）
- [ ] ResourceManager 深度集成

### 公共组件（共享）
- [ ] `TimeSeriesDB::query()` 支持窗口语义
- [ ] `ResourceManager` 添加计算引擎支持
- [ ] `EventBus` 事件类型扩展（window_completed）
- [ ] CMake 双模式构建配置
- [ ] 性能对比测试框架

## 性能优化要点

1. **批量查询**
   ```cpp
   // ❌ 逐条查询
   for (auto ts : timestamps) {
       auto data = db->query({.timestamp = ts});
   }
   
   // ✅ 批量查询
   auto data = db->query({.time_range = {start, end}});
   ```

2. **零拷贝共享**
   ```cpp
   // ✅ 使用 shared_ptr 避免数据拷贝
   std::shared_ptr<std::vector<TimeSeriesData>> window_data;
   ```

3. **异步写入**
   ```cpp
   // ✅ 结果异步写入，不阻塞计算
   handle->submitTask([result]() {
       db->insert("join_results", result);
   });
   ```

## 验收标准

### 功能验收
| 检查项 | 插件模式 | 深度融合模式 |
|-------|---------|-------------|
| 正确性 | ✅ 与 PECJ 原始结果一致 | ✅ 与插件模式结果一致 |
| 数据输入 | `feedData()` | `db->insert()` |
| 乱序支持 | ✅ | ✅ |
| 窗口语义 | ✅ | ✅ |
| 故障恢复 | 部分支持 | ✅ 完整支持 |

### 性能验收
| 指标 | 插件模式（Baseline） | 深度融合模式（目标） |
|-----|---------------------|-------------------|
| 吞吐量 | 100% (基准) | ≥ 90% |
| 延迟 P99 | 基准值 | ≤ 基准值 × 1.2 |
| 内存占用 | 基准值 | ≤ 基准值 × 0.7 (减少30%+) |
| 线程数 | 不可控 | 精确控制在配额内 |
| CPU 使用率 | 基准值 | 相近 |

### 对比实验验收
- [ ] 在 1M, 10M, 100M 事件规模下对比
- [ ] 在 0%, 10%, 30% 乱序率下对比
- [ ] 在 2, 4, 8 并发流下对比
- [ ] 生成性能对比报告（包含可视化图表）
- [ ] 证明深度融合模式的内存优势（≥30% 减少）

## 参考文档

- 完整设计文档：`DESIGN_DOC_SAGETSDB_PECJ.md`
- 资源管理指南：`RESOURCE_MANAGER_GUIDE.md`
- LSM-Tree 实现：`LSM_TREE_IMPLEMENTATION.md`
- 示例代码：`examples/pecj_replay_demo.cpp`

## 性能对比实验完整流程

### 步骤 1: 构建双模式二进制
```bash
# 自动化脚本
./scripts/build_dual_mode.sh

# 或手动构建
mkdir -p build_{plugin,integrated}

# 插件模式
cd build_plugin
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=PLUGIN -DPECJ_DIR=/path/to/PECJ
make -j$(nproc)

# 深度融合模式
cd ../build_integrated
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED -DPECJ_DIR=/path/to/PECJ
make -j$(nproc)
```

### 步骤 2: 准备测试数据
```bash
./scripts/generate_test_data.sh \
    --scale small,medium,large \
    --ooo-ratio 0,10,30 \
    --output data/
```

### 步骤 3: 运行基准测试
```bash
# 插件模式
./build_plugin/bin/pecj_benchmark \
    --data data/medium_ooo10.bin \
    --config configs/pecj_test.json \
    --output results/plugin_medium_ooo10.json

# 深度融合模式
./build_integrated/bin/pecj_benchmark \
    --data data/medium_ooo10.bin \
    --config configs/pecj_test.json \
    --output results/integrated_medium_ooo10.json
```

### 步骤 4: 生成对比报告
```bash
python3 scripts/compare_performance.py \
    --plugin results/plugin_*.json \
    --integrated results/integrated_*.json \
    --output report.html
```

### 预期实验结果

| 维度 | 插件模式 | 深度融合模式 | 变化 |
|------|---------|-------------|------|
| 吞吐量 | 1.0M events/s | 0.95M events/s | -5% ✅ |
| 延迟 P99 | 50ms | 55ms | +10% ✅ |
| 内存峰值 | 4096MB | 2560MB | **-37%** ✅✅ |
| 线程数 | 16 (不可控) | 4 (精确控制) | ✅ |
| 数据一致性 | 可能不一致 | 100% 一致 | ✅✅ |

**结论**：深度融合模式在内存和资源控制上有显著优势，性能略有下降但在可接受范围内。

## 版本信息

- **设计版本**：v3.0 (Dual Mode Integration)
- **更新日期**：2024-12-04
- **目标分支**：`pecj_dual_mode_integration`
- **实施周期**：6-8 周（分4个阶段）
- **实验周期**：2-3 周（性能对比验证）
