# sageTSDB + PECJ 集成设计文档

本文档详细描述了 sageTSDB 中间件与 PECJ (Predictive Error-bounded Computation for Joins) 及故障检测算法的集成设计、运行机制及状态管理。

## 1. 总体架构

sageTSDB 采用插件化架构，通过 `PluginManager` 管理所有算法组件。PECJ 和故障检测算法均被封装为独立的插件（Adapter），通过统一的接口与宿主系统交互，并通过 `EventBus` 进行解耦通信。

### 核心组件
*   **PluginManager**: 负责插件的加载、初始化、生命周期管理及资源分配（线程池、内存限制）。
*   **EventBus**: 提供发布/订阅机制，实现插件间、插件与核心系统间的异步通信。
*   **PECJAdapter**: 封装 PECJ 库，负责数据格式转换和 PECJ 算子管理。
*   **FaultDetectionAdapter**: 封装故障检测逻辑（Z-Score, VAE），负责实时异常监测。

## 2. 运行机制与多线程模型

### 2.1 多线程执行模型
系统采用分层多线程模型，确保高吞吐量和低延迟：

1.  **主摄取线程 (Ingestion Thread)**:
    *   负责接收外部数据流（如 Kafka, 网络端口）。
    *   调用 `PluginManager::feedData` 将数据分发给各激活插件。
    *   设计为非阻塞，仅做简单分发。

2.  **插件管理线程池 (Plugin Worker Pool)**:
    *   由 `PluginManager` 维护（配置项 `thread_pool_size`）。
    *   负责执行插件的 `process()` 任务，处理繁重的计算逻辑。
    *   PECJ 和故障检测的计算任务可在此池中并发执行。

3.  **PECJ 内部线程**:
    *   PECJ 算法本身支持多线程（配置项 `threads`）。
    *   用于并行处理 Join 操作、状态更新和变分推断计算。

4.  **EventBus 分发线程**:
    *   独立的后台线程，负责从事件队列中取出事件（如 `WINDOW_TRIGGERED`, `RESULT_READY`）并回调订阅者。

### 2.2 Window 间的可见性与交互
在流处理中，窗口（Window）状态的可见性通过以下机制保证：

*   **内部可见性 (PECJ)**: PECJ 内部维护滑动窗口（Sliding Window）。S 流和 R 流的元组在时间窗口内是互相可见的，PECJ 通过 Watermark 机制触发窗口计算，确保乱序数据在窗口关闭前被正确处理。
*   **组件间可见性 (EventBus)**:
    *   当 PECJ 完成一个窗口的计算时，会发布 `WINDOW_TRIGGERED` 或 `RESULT_READY` 事件。
    *   故障检测插件或下游组件订阅这些事件，从而“看到”窗口的计算结果。
    *   **共享内存 (Zero-Copy)**: 通过 `std::shared_ptr` 传递数据载荷，确保不同插件看到的窗口数据是同一份物理内存，避免拷贝。

## 3. PECJ 算法启动与集成

### 3.1 启动流程
1.  **加载插件**: 用户通过 `PluginManager::loadPlugin("pecj", config)` 请求加载。
2.  **初始化 (initialize)**:
    *   `PECJAdapter` 解析配置参数（`windowLen`, `slideLen`, `wmTag` 等）。
    *   调用 `initializePECJ()`。
    *   在内部实例化 `OoOJoin::AbstractOperator`（PECJ 的核心算子）。
    *   配置 PECJ 的内部参数（如 Join 策略、缓冲区大小）。
3.  **启动 (start)**: 标记插件为运行状态，准备接收数据。

### 3.2 数据流转
*   外部 `TimeSeriesData` -> `PECJAdapter::feedData`。
*   **格式转换**: 适配器将 `TimeSeriesData` 转换为 PECJ 专用的 `OoOJoin::TrackTuple`。
    *   自动识别流类型（S流/R流），通常基于 Tag 或时间戳奇偶性。
*   **注入**: 调用 PECJ 算子的 `feedTuple` 接口注入数据。

## 4. 故障检测算法启动与运行

### 4.1 启动流程
1.  **配置**: 指定检测方法（`method`: "zscore" 或 "vae"）、阈值（`threshold`）、窗口大小（`window_size`）。
2.  **模型初始化**:
    *   若选择 VAE，加载预训练模型或初始化 `TROCHPACK_VAE::LinearVAE`。
    *   若选择 Z-Score，初始化统计缓冲区。

### 4.2 运行逻辑
*   **数据摄取**: 接收 `TimeSeriesData`。
*   **滑动窗口维护**: 将新数据推入历史双端队列 (`std::deque`)，移除过期数据。
*   **检测计算**:
    *   **Z-Score**: 计算窗口内均值和标准差，判断当前值偏离程度。
    *   **VAE**: 将窗口数据输入编码器-解码器，计算重构误差 (Reconstruction Error) 作为异常分数。
*   **结果发布**: 若分数超过阈值，生成 `DetectionResult` 并通过 EventBus 发布告警。

## 5. 中间状态维护

系统中的中间状态分为三类，分别由不同层级维护：

### 5.1 PECJ 中间状态 (由 PECJ 库内部维护)
*   **Join Buffers**: S 流和 R 流的待匹配元组缓冲区。
*   **Watermark**: 当前处理的时间水位线，决定窗口何时关闭。
*   **Join State**: 已匹配的元组对和未匹配的保留集。
*   **维护方式**: 适配器不直接干预，仅通过 `feedData` 驱动状态更新，通过 `getStats` 获取状态摘要（如延迟、处理计数）。

### 5.2 故障检测中间状态 (由 Adapter 维护)
*   **历史窗口**: `std::deque<TimeSeriesData>`，用于计算统计特征或作为 ML 模型输入。
*   **模型参数**: VAE 的权重矩阵或统计模型的均值/方差。
*   **维护方式**: 
    *   内存驻留。
    *   `updateModel()` 接口支持在线更新模型参数。
    *   `reset()` 接口可清空历史状态。

### 5.3 系统级中间状态 (由 sageTSDB 维护)
*   **事件队列**: 待分发的异步事件。
*   **插件状态机**: 各插件的 `Initialized`, `Running`, `Stopped` 状态。
*   **资源监控**: 内存使用率、线程负载等。

## 6. 示例配置 (config.csv / JSON)

```json
{
  "pecj": {
    "windowLen": "1000000",   // 窗口长度 (us)
    "slideLen": "500000",     // 滑动步长 (us)
    "threads": "4",           // PECJ 内部线程数
    "latenessMs": "100",      // 允许最大乱序延迟 (ms)
    "operator": "IMA",        // 算子类型: IAWJ, IMA, MSWJ, AI, LinearSVI, MeanAQP, SHJ, PRJ
    "sLen": "10000",          // S流缓冲区大小
    "rLen": "10000"           // R流缓冲区大小
  },
  "fault_detection": {
    "method": "vae",          // 检测算法: zscore, vae, hybrid
    "window_size": "100",     // 历史窗口点数
    "threshold": "0.95"       // 异常阈值
  }
}
```

## 7. 构建与运行指南

### 7.1 构建 PECJ

```bash
# 1. 进入 PECJ 目录
cd /path/to/PECJ

# 2. 创建构建目录
mkdir -p build && cd build

# 3. 配置并构建
cmake ..
make -j$(nproc)
```

### 7.2 构建 sageTSDB (带 PECJ 集成)

```bash
# 1. 进入 sageTSDB 目录
cd /path/to/sageTSDB

# 2. 创建构建目录
mkdir -p build && cd build

# 3. 配置 (Stub 模式 - 不需要 PECJ 库)
cmake -DPECJ_DIR=/path/to/PECJ ..

# 4. 配置 (Full 模式 - 需要 PECJ 库)
cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON ..

# 5. 构建
make -j$(nproc)

# 6. 运行测试
ctest --output-on-failure
```

### 7.3 代码示例

```cpp
#include "sage_tsdb/plugins/plugin_manager.h"
#include "sage_tsdb/plugins/adapters/pecj_adapter.h"

int main() {
    using namespace sage_tsdb::plugins;
    
    // 1. 创建插件管理器
    PluginManager plugin_mgr;
    plugin_mgr.initialize();
    
    // 2. 配置资源
    PluginManager::ResourceConfig res_config;
    res_config.thread_pool_size = 8;
    res_config.max_memory_mb = 2048;
    res_config.enable_zero_copy = true;
    plugin_mgr.setResourceConfig(res_config);
    
    // 3. 配置 PECJ
    PluginConfig pecj_config = {
        {"windowLen", "1000000"},
        {"slideLen", "500000"},
        {"operator", "IMA"},
        {"sLen", "10000"},
        {"rLen", "10000"},
        {"latenessMs", "100"}
    };
    
    // 4. 加载并启动 PECJ
    plugin_mgr.loadPlugin("pecj", pecj_config);
    plugin_mgr.startAll();
    
    // 5. 喂入数据
    for (int i = 0; i < 1000; i++) {
        auto data = std::make_shared<TimeSeriesData>();
        data->timestamp = i * 1000;
        data->value = 100.0 + (i % 50);
        data->tags["stream"] = (i % 2 == 0) ? "S" : "R";
        data->tags["key"] = std::to_string(i % 10);
        
        plugin_mgr.feedDataToAll(data);
    }
    
    // 6. 获取结果
    auto stats = plugin_mgr.getAllStats();
    for (const auto& [plugin, plugin_stats] : stats) {
        std::cout << "Plugin: " << plugin << std::endl;
        for (const auto& [key, value] : plugin_stats) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    }
    
    // 7. 停止
    plugin_mgr.stopAll();
    
    return 0;
}
```

## 8. 架构图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           sageTSDB System                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌──────────────┐     ┌──────────────────────────────────────────────┐  │
│  │   Ingestion  │     │              PluginManager                    │  │
│  │    Thread    │────▶│  ┌─────────────────────────────────────────┐ │  │
│  └──────────────┘     │  │            EventBus                      │ │  │
│                       │  │  (Pub/Sub, Zero-Copy shared_ptr)         │ │  │
│                       │  └─────────────────────────────────────────┘ │  │
│                       │                    │                          │  │
│                       │         ┌──────────┴──────────┐               │  │
│                       │         ▼                     ▼               │  │
│                       │  ┌─────────────┐       ┌─────────────┐        │  │
│                       │  │ PECJAdapter │       │ FaultDetect │        │  │
│                       │  │             │       │   Adapter   │        │  │
│                       │  │ ┌─────────┐ │       │             │        │  │
│                       │  │ │ PECJ    │ │       │ ┌─────────┐ │        │  │
│                       │  │ │Operator │ │       │ │ Z-Score │ │        │  │
│                       │  │ │(OoOJoin)│ │       │ │   VAE   │ │        │  │
│                       │  │ └─────────┘ │       │ └─────────┘ │        │  │
│                       │  └─────────────┘       └─────────────┘        │  │
│                       └──────────────────────────────────────────────┘  │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │                        Data Flow                                   │   │
│  │  TimeSeriesData ──▶ convertToTrackTuple ──▶ feedTupleS/R          │   │
│  │                                                                    │   │
│  │  Window Result  ◀── getResult/getAQPResult ◀── PECJ Internal      │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘

Window Visibility:
┌───────────────────────────────────────────────────────────────┐
│  S Stream: [t1, t2, t3, ...]     Window [T, T+W]              │
│  R Stream: [t1', t2', t3', ...]                               │
│                    ▼                                          │
│            Watermark Trigger                                  │
│                    ▼                                          │
│  Join Result: {(s_i, r_j) | key_match && time_in_window}     │
│                    ▼                                          │
│  EventBus: WINDOW_TRIGGERED event ──▶ FaultDetection         │
└───────────────────────────────────────────────────────────────┘
```

## 9. 支持的 PECJ 算子类型

| 算子名称 | 描述 | 适用场景 |
|---------|------|---------|
| IAWJ | Interval-Aware Window Join | 基础窗口连接 |
| IMA | IMA-based AQP | 近似查询处理，支持乱序 |
| MSWJ | Multi-Stream Window Join | 多流连接 |
| AI | AI-enhanced Operator | 机器学习增强 |
| LinearSVI | Linear Stochastic Variational Inference | 变分推断预测 |
| MeanAQP | Mean-based AQP | 均值近似 |
| SHJ | Symmetric Hash Join | 对称哈希连接 |
| PRJ | Partitioned Range Join | 分区范围连接 |
