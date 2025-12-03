# sageTSDB 管理型 PECJ 集成设计文档

版本: v2.0 (Resource-Managed Architecture)
更新日期: 2024-12-03
分支: `pecj_resource_integration`

摘要
---
本文档描述将 PECJ 作为受控算子集成到 sageTSDB 的方案。核心原则是由 sageTSDB 统一管理所有运行资源（线程、内存、GPU、模型生命周期），并把传统的解耦（插件独立运行）方案保留为实验对比的 baseline。文档聚焦资源代管设计、最小接口契约、构建/运行开关与迁移步骤，删除冗余实现细节和示例代码。

设计目标
---
- 以 sageTSDB 的 ResourceManager 为中心，统一管理线程池、内存配额、设备（GPU）与模型加载。
- 在构建时通过 CMake 参数决定集成深度：Stub（默认）、Integrated（资源代管/生产）、Baseline（原有解耦，仅用于对比）。
- 保持外部接口简单：PluginManager 只需申请资源、发送数据、接收结果；PECJ 的内部实现通过适配器封装。
- 强制可观测性：资源使用、队列长度、延迟、错误率必须上报。

模式与优先级
---
1. Integrated（推荐，生产）
     - sageTSDB 分配并强制执行资源配额（线程、内存、GPU），PECJ 在这些约束下运行。
     - 目的：防止线程/内存爆炸，集中监控与运维控制。
2. Stub（默认，开发/CI）
     - 仅编译适配器接口与轻量 stub 行为，不依赖 PECJ 库，便于快速编译与单元测试。
## 3. 集成方案及实施摘要

为保持简洁，本节以要点方式说明 Integrated（资源代管）模式的实现约定，并把原有解耦模式作为 baseline 仅用于实验对比。

核心约定：
- 不修改 PECJ 外部 API；通过 `PECJAdapter` 做适配和受控运行。
- 在 Integrated 模式下，ResourceManager 为每个插件分配 ResourceHandle，包含线程配额、内存上限和可见 GPU 列表。
- 插件通过 ResourceHandle 在 sageTSDB 提供的受控执行上下文（线程池或任务队列）提交工作，不得自行无限制创建线程。

最小接口契约
---
PECJAdapter 必须实现：
- bool init(const PluginConfig &cfg, const ResourceRequest &req);
- void start();
- void stop();
- void onData(std::shared_ptr<TimeSeriesData> data);
- PluginStatus status() const; // 返回资源使用、队列长度、uptime 与最后错误

ResourceRequest 字段（示例）：
```
struct ResourceRequest {
    int requested_threads = 0;
    uint64_t max_memory_bytes = 0;
    std::vector<int> gpu_ids; // optional
    std::string model_path; // optional
};
```

CMake / 构建开关（建议）
---
- `-DPECJ_DIR=/path/to/PECJ`：PECJ 源或构建路径
- `-DPECJ_FULL_INTEGRATION=ON|OFF`：是否在构建时链接 PECJ（默认 OFF）
- `-DPECJ_MODE=Integrated|Stub|Baseline`：显式选择运行模式（优先级与上文一致）
- `-DPECJ_MANAGED_THREADS=<n>`：当 >0 时建议 ResourceManager 将该线程数作为默认配额

资源代管关键点
---
- 线程：使用 ResourceManager 提供的线程池或任务队列；插件应以任务/回调方式提交计算。
- 内存：实现软/硬阈值监控；超阈值时触发降级策略。
- GPU/模型：ResourceManager 控制 CUDA 可见设备并缓存模型实例，避免重复加载与 OOM。
- 监控：每个插件按周期上报 metrics（threads_used, memory_used_bytes, queue_length, tuples_processed, avg_latency_ms）。

降级与运维
---
- 监控到资源异常时优先收紧配额，必要时切换到 Stub 模式并报警。
- 提供运维 API 支持热启/停插件，以及查询/调整 ResourceRequest。

实施路线（最小可行步）
---
1. 定义 `resource_manager.h`（ResourceRequest / ResourceHandle / ResourceManager 接口）并提交草案。
2. 调整 `pecj_adapter.h` 的 init 签名以接收 ResourceRequest；实现 Stub 版本并提交。
3. 在 PluginManager 中集成 ResourceManager：加载插件时发起资源申请，并将 ResourceHandle 注入插件。
4. 实现最小 ResourceManager（线程池 + 内存监控），运行 CI 验证（Stub 模式下）。
5. 在含 PECJ 的环境中启用 Integrated 模式，验证资源限额与功能兼容性。

验证要点（验收准则）
---
- 默认构建（不启用 PECJ）通过所有单元测试。
- Integrated 模式下全局线程数与内存使用不超过配置阈值。
- EventBus 语义与现有插件兼容，无需下游改造。
- Baseline 与 Integrated 给出对比报告（吞吐 vs 延迟 vs 资源使用）。

后续工作与注意事项
---
- 补充 API 文档（函数签名、错误码、metrics schema）。
- 在实现阶段优先以小步快频提交（Stub -> ResourceManager -> Integrated）。
- CI 环境建议保留一个带 PECJ 的测试 runner 用于集成验证。

---
文档更新：本文件为精简版设计稿，冗余细节与历史实现保留在 `DESIGN_DOC_SAGETSDB_PECJ.md.backup`。
    *   根据 `data.tags["stream"]` 判断流类型（S 或 R）。
    *   若未设置 stream 标签，使用时间戳奇偶性：`(timestamp/1000) % 2 == 0` → S 流。
    *   将数据和流类型标记 `(data, is_s_stream)` 推入异步队列 `data_queue_`。
    *   通过条件变量 `queue_cv_` 唤醒工作线程。
    
*   **工作线程处理** (`workerLoop`):
    *   从 `data_queue_` 中取出数据。
    *   根据流类型调用 `feedStreamS()` 或 `feedStreamR()`。
    *   每次处理后检查 PECJ 的 `getResult()`，若有新结果则发布事件。
    
*   **格式转换** (`convertToTrackTuple`):
    *   提取 `key`：优先从 `data.tags["key"]` 读取，否则对 `measurement` 名称进行哈希。
    *   提取 `value`：调用 `data.as_double()`。
    *   创建 `OoOJoin::TrackTuple(key, value)`。
    *   设置 `eventTime = data.timestamp`（事件时间）。
    *   设置 `arrivalTime`：当前系统时间相对于 `time_base_` 的微秒偏移。
    *   设置 `streamId`：S 流为 0，R 流为 1。
    
*   **注入 PECJ**: 
    *   调用 `pecj_operator_->feedTupleS(track_tuple)` 或 `feedTupleR(track_tuple)`。
    *   更新统计计数器：`tuples_processed_s_`, `tuples_processed_r_`, `total_latency_us_`。

## 4. 故障检测算法启动与运行

### 4.1 启动流程
1.  **加载插件**: 
    *   通过 `PluginManager::loadPlugin("fault_detection", config)` 加载。
    *   `PluginRegistry` 创建 `FaultDetectionAdapter` 实例。
    
2.  **配置解析**:
    *   `method`: 检测方法（"zscore", "vae", "hybrid"）。
    *   `threshold`: 异常阈值（Z-Score 标准差倍数或 VAE 重构误差阈值）。
    *   `window_size`: 历史窗口大小（样本点数）。
    
3.  **模型初始化** (`initializeModel`):
    *   **VAE 模式**: 加载预训练模型或初始化 `TROCHPACK_VAE::LinearVAE`（需要 PyTorch C++ 前端）。
    *   **Z-Score 模式**: 初始化统计变量 `running_mean_`, `running_variance_`, `sample_count_`。
    *   初始化历史缓冲区 `value_history_`（`std::deque<double>`）。
    
4.  **启动**: 
    *   设置 `running_ = true`，准备接收数据。
    *   可选：订阅 EventBus 的 `WINDOW_TRIGGERED` 事件以监控 PECJ 窗口结果。

### 4.2 运行逻辑
*   **数据摄取**: 
    *   `FaultDetectionAdapter::feedData(const TimeSeriesData& data)` 接收数据。
    *   提取数值：`double value = data.as_double()`。
    
*   **滑动窗口维护**:
    *   将新值推入 `value_history_`（`std::deque<double>`）。
    *   若窗口大小超过 `window_size_`，移除最旧的数据点。
    *   更新统计变量（`running_mean_`, `running_variance_`）。
    
*   **检测计算**:
    *   **Z-Score 方法** (`detectZScore`):
        1. 计算窗口内均值 $\mu$ 和标准差 $\sigma$。
        2. 计算 Z 分数：$z = \frac{|x - \mu|}{\sigma}$。
        3. 若 $z >$ `threshold`，标记为异常。
        
    *   **VAE 方法** (`detectVAE`):
        1. 将窗口数据归一化为固定长度向量。
        2. 输入 VAE 编码器获取潜在表示 $z$。
        3. 解码器重构输入：$\hat{x} = \text{decoder}(z)$。
        4. 计算重构误差：$e = \|\hat{x} - x\|_2$。
        5. 若 $e >$ `threshold`，标记为异常。
        
    *   **Hybrid 方法**: 结合 Z-Score 和 VAE 的结果，取加权平均或逻辑 OR。
    
*   **结果记录与发布**:
    *   构造 `DetectionResult` 结构体：
        *   `timestamp`: 当前时间戳
        *   `is_anomaly`: 布尔标志
        *   `anomaly_score`: 异常分数
        *   `severity`: 严重程度（NORMAL, WARNING, CRITICAL）
    *   存入 `detection_history_`（限制最大历史记录数 `max_history_size_`）。
    *   若检测到异常，通过 EventBus 发布 `ERROR_OCCURRED` 事件。
    *   更新统计计数器：`total_samples_`, `anomalies_detected_`。

## 5. 中间状态维护

系统中的中间状态分为三类，分别由不同层级维护：

### 5.1 PECJ 中间状态 (由 PECJ 库内部维护)
*   **Join Buffers**: 
    *   S 流缓冲区：容量为 `sLen`（可配置）。
    *   R 流缓冲区：容量为 `rLen`（可配置）。
    *   存储待匹配的 `TrackTuple` 对象。
    
*   **Watermark**: 
    *   当前处理的时间水位线，由 PECJ 内部的 `WaterMarker` 组件维护。
    *   决定窗口何时关闭，允许最大乱序延迟为 `latenessMs`。
    
*   **Join State**: 
    *   已匹配的元组对：存储在内部哈希表或 B-Tree 中。
    *   未匹配的保留集：等待后续到达的元组。
    *   变分推断状态：IMA、LinearSVI 等算子维护的均值/方差估计。
    
*   **维护方式**: 
    *   适配器**不直接访问**这些内部状态，保持解耦。
    *   仅通过 `feedTupleS/R` 驱动状态更新。
    *   通过以下接口获取结果：
        *   `getResult()`: 返回精确 Join 计数。
        *   `getApproximateResult()`: 返回 AQP 估计（支持 IMA、MeanAQP 等算子）。
        *   `getStats()`: 返回统计摘要（处理延迟、缓冲区使用率等）。
        *   `getTimeBreakdown()`: 返回时间分解（Join 时间、推断时间、I/O 时间）。

### 5.2 故障检测中间状态 (由 Adapter 维护)
*   **历史窗口**: 
    *   `value_history_`（`std::deque<double>`）：存储最近 `window_size_` 个数据点。
    *   用于 Z-Score 统计计算或作为 VAE 模型输入。
    *   线程安全：通过 `results_mutex_` 保护。
    
*   **统计变量**:
    *   `running_mean_`: 滚动均值。
    *   `running_variance_`: 滚动方差。
    *   `sample_count_`: 已处理样本总数。
    *   使用 Welford 在线算法更新，避免数值不稳定。
    
*   **ML 模型参数**:
    *   `vae_model_`（`std::shared_ptr<TROCHPACK_VAE::LinearVAE>`）：VAE 模型实例。
    *   编码器权重、解码器权重、潜在空间维度。
    *   可选：与 PECJ 共享模型实例（通过配置指定）。
    
*   **检测历史**:
    *   `detection_history_`（`std::deque<DetectionResult>`）：记录最近的检测结果。
    *   最大容量：`max_history_size_`（默认 1000）。
    *   用于查询历史异常、生成报告、计算准确率。
    
*   **维护方式**: 
    *   内存驻留，线程安全（`stats_mutex_` 保护）。
    *   `updateModel(training_data)`: 支持在线学习，更新 VAE 参数。
    *   `reset()`: 清空历史窗口、重置统计变量、清除检测历史。
    *   `getDetectionResults(count)`: 获取最近 N 条检测结果。

### 5.3 系统级中间状态 (由 sageTSDB 维护)
*   **事件队列** (`EventBus`):
    *   `event_queue_`（`std::queue<Event>`）：待分发的异步事件。
    *   使用 `queue_mutex_` 和 `cv_` 实现生产者-消费者模式。
    *   支持的事件类型：`DATA_INGESTED`, `WINDOW_TRIGGERED`, `RESULT_READY`, `ERROR_OCCURRED`, `CUSTOM`。
    
*   **插件注册表** (`PluginRegistry`):
    *   单例模式：`PluginRegistry::instance()`。
    *   存储插件工厂函数：`std::map<std::string, PluginFactory>`。
    *   自动注册机制：通过全局静态变量在程序启动时注册插件。
    
*   **插件实例管理** (`PluginManager`):
    *   `plugins_`（`std::unordered_map<std::string, PluginPtr>`）：已加载的插件实例。
    *   `plugin_enabled_`（`std::unordered_map<std::string, bool>`）：插件启用状态。
    *   线程安全：`plugins_mutex_` 保护。
    
*   **插件状态机**:
    *   每个插件维护独立的状态标志：
        *   `initialized_`（`std::atomic<bool>`）：是否已初始化。
        *   `running_`（`std::atomic<bool>`）：是否正在运行。
    *   状态转换：`Unloaded` → `Initialized` → `Running` → `Stopped` → `Unloaded`。
    
*   **资源配置** (`ResourceConfig`):
    *   `max_memory_mb`: 所有插件的最大内存限制（当前仅提示性，未强制执行）。
    *   `thread_pool_size`: 建议的线程池大小（当前插件使用独立线程）。
    *   `enable_zero_copy`: 是否启用零拷贝数据传递（默认 true）。
    *   `event_queue_size`: EventBus 队列最大容量（用于背压控制）。
    
*   **事件订阅管理**:
    *   `subscribers_`（`std::unordered_map<EventType, std::vector<Subscriber>>`）：按事件类型分组的订阅者列表。
    *   `next_subscriber_id_`: 自增订阅 ID 生成器。
    *   订阅者结构：`{id: int, callback: EventCallback}`。

## 6. 示例配置 (PluginConfig / JSON)

### 6.1 PECJ 插件配置
```cpp
PluginConfig pecj_config = {
    // 窗口配置（时间单位：微秒 μs）
    {"windowLen", "1000000"},      // 窗口长度：1秒 = 1,000,000 μs
    {"slideLen", "500000"},        // 滑动步长：500毫秒
    {"timeStep", "1000"},          // 内部时间步长：1毫秒（可选）
    
    // 乱序处理
    {"latenessMs", "100"},         // 允许最大乱序延迟：100毫秒
    
    // 算子类型（必选）
    {"operator", "IMA"},           // 可选值见下表
    
    // 缓冲区配置（元组数量）
    {"sLen", "10000"},             // S流缓冲区大小
    {"rLen", "10000"},             // R流缓冲区大小
    
    // 性能调优
    {"threads", "4"}               // PECJ 内部并行线程数
};
```

### 6.2 故障检测插件配置
```cpp
PluginConfig fault_config = {
    // 检测方法
    {"method", "zscore"},          // 可选: "zscore", "vae", "hybrid"
    
    // 窗口配置
    {"window_size", "50"},         // 历史窗口：50个数据点
    
    // 阈值配置
    {"threshold", "3.0"},          // Z-Score: 标准差倍数
                                   // VAE: 重构误差阈值
    
    // VAE 专用配置（可选）
    {"model_path", "/path/to/vae.pt"},   // 预训练模型路径
    {"latent_dim", "16"},          // 潜在空间维度
    {"learning_rate", "0.001"}     // 在线学习率
};
```

### 6.3 资源管理配置
```cpp
PluginManager::ResourceConfig res_config;
res_config.thread_pool_size = 8;         // 建议线程池大小
res_config.max_memory_mb = 2048;         // 最大内存限制（MB）
res_config.enable_zero_copy = true;      // 启用零拷贝（推荐）
res_config.event_queue_size = 10000;     // EventBus 队列容量
```

### 6.4 完整 JSON 配置示例
```json
{
  "resources": {
    "thread_pool_size": 8,
    "max_memory_mb": 2048,
    "enable_zero_copy": true,
    "event_queue_size": 10000
  },
  "plugins": {
    "pecj": {
      "windowLen": "1000000",
      "slideLen": "500000",
      "operator": "IMA",
      "threads": "4",
      "latenessMs": "100",
      "sLen": "10000",
      "rLen": "10000"
    },
    "fault_detection": {
      "method": "vae",
      "window_size": "100",
      "threshold": "0.95",
      "model_path": "./models/vae_pretrained.pt"
    }
  }
}
```

## 7. 构建与运行指南

### 7.0 构建模式说明

sageTSDB 支持两种 PECJ 集成模式：

#### **Stub 模式（默认）**
*   **用途**: 开发、测试、CI/CD 流水线
*   **特点**: 
    *   不依赖 PECJ 库和 PyTorch
    *   `PECJAdapter` 仅提供接口桩（Stub）
    *   可以编译、测试核心功能
    *   打印配置信息但不执行实际 Join
*   **启用方式**: 默认或 `cmake ..`

#### **Full 模式（生产）**
*   **用途**: 生产部署、性能测试、完整功能演示
*   **特点**:
    *   完整集成 PECJ 库（`OoOJoin::AbstractOperator`）
    *   支持所有 8 种算子
    *   需要 PyTorch C++ 前端
    *   需要 C++20 和旧版 ABI（`_GLIBCXX_USE_CXX11_ABI=0`）
*   **启用方式**: `cmake -DPECJ_FULL_INTEGRATION=ON ..`

### 7.1 构建 PECJ（仅 Full 模式需要）

```bash
# 1. 进入 PECJ 目录
cd /path/to/PECJ

# 2. 安装依赖
# PyTorch (必需)
pip install torch

# Log4cxx (可选，用于日志)
sudo apt-get install liblog4cxx-dev

# 3. 创建构建目录
mkdir -p build && cd build

# 4. 配置并构建
cmake ..
make -j$(nproc)

# 5. 验证构建（可选）
cd test
./test_pecj
```

### 7.2 构建 sageTSDB (带 PECJ 集成)

```bash
# 1. 进入 sageTSDB 目录
cd /path/to/sageTSDB

# 2. 创建构建目录
mkdir -p build && cd build

# 3. 配置 (Stub 模式 - 不需要 PECJ 库，适合开发和测试)
cmake -DPECJ_DIR=/path/to/PECJ ..
# 或者不指定 PECJ_DIR（将使用默认 Stub 实现）
cmake ..

# 4. 配置 (Full 模式 - 需要 PECJ 库和 PyTorch)
# 确保已安装 PyTorch: pip install torch
cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON ..

# 5. 构建
make -j$(nproc)

# 6. 运行测试
ctest --output-on-failure

# 7. 运行集成演示（需要 PECJ 数据集）
cd build/examples
./integrated_demo \
    --s-file /path/to/PECJ/benchmark/datasets/sTuple.csv \
    --r-file /path/to/PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 10000 \
    --detection zscore \
    --threshold 3.0
```

**重要依赖说明**:
*   **C++20 编译器**: GCC 10+ 或 Clang 12+
*   **CMake 3.15+**
*   **PyTorch C++ 前端** (Full 模式): 
    *   通过 `python3 -c "import torch; print(torch.utils.cmake_prefix_path)"` 自动检测
    *   或手动设置 `CMAKE_PREFIX_PATH`
*   **ABI 兼容性**: 使用 `_GLIBCXX_USE_CXX11_ABI=0` 确保与 PyTorch 兼容

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
    res_config.event_queue_size = 10000;
    plugin_mgr.setResourceConfig(res_config);
    
    // 3. 配置 PECJ
    PluginConfig pecj_config = {
        {"windowLen", "1000000"},      // 1秒窗口 (微秒)
        {"slideLen", "500000"},        // 500毫秒滑动 (微秒)
        {"operator", "IMA"},           // IMA 算子
        {"sLen", "10000"},             // S流缓冲区
        {"rLen", "10000"},             // R流缓冲区
        {"latenessMs", "100"},         // 100毫秒乱序容忍
        {"threads", "4"}               // PECJ内部线程数
    };
    
    // 4. 配置故障检测
    PluginConfig fault_config = {
        {"method", "zscore"},          // 检测方法
        {"window_size", "50"},         // 历史窗口大小
        {"threshold", "3.0"}           // 异常阈值
    };
    
    // 5. 加载并启动插件
    plugin_mgr.loadPlugin("pecj", pecj_config);
    plugin_mgr.loadPlugin("fault_detection", fault_config);
    plugin_mgr.startAll();
    
    // 6. 订阅事件（可选）
    auto& event_bus = plugin_mgr.getEventBus();
    int sub_id = event_bus.subscribe(EventType::WINDOW_TRIGGERED, 
        [](const Event& evt) {
            std::cout << "Window triggered from " << evt.source << std::endl;
        });
    
    // 7. 喂入数据（零拷贝）
    for (int i = 0; i < 1000; i++) {
        auto data = std::make_shared<TimeSeriesData>();
        data->timestamp = i * 1000;
        data->value = 100.0 + (i % 50);
        data->tags["stream"] = (i % 2 == 0) ? "S" : "R";
        data->tags["key"] = std::to_string(i % 10);
        
        // 零拷贝分发给所有插件
        plugin_mgr.feedDataToAll(data);
    }
    
    // 8. 等待处理完成
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 9. 获取统计信息
    auto stats = plugin_mgr.getAllStats();
    for (const auto& [plugin, plugin_stats] : stats) {
        std::cout << "Plugin: " << plugin << std::endl;
        for (const auto& [key, value] : plugin_stats) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    }
    
    // 10. 获取故障检测结果（可选）
    auto fd_plugin = std::dynamic_pointer_cast<FaultDetectionAdapter>(
        plugin_mgr.getPlugin("fault_detection"));
    if (fd_plugin) {
        auto results = fd_plugin->getDetectionResults(10);
        std::cout << "\nRecent anomalies: " << results.size() << std::endl;
    }
    
    // 11. 停止并清理
    event_bus.unsubscribe(sub_id);
    plugin_mgr.stopAll();
    
    return 0;
}
```

## 8. 插件注册与工厂模式

### 8.1 插件注册机制
sageTSDB 使用单例模式的 `PluginRegistry` 管理插件工厂函数：

```cpp
// 在 pecj_adapter.cpp 中自动注册
namespace {
    // 全局静态变量，程序启动时自动执行
    bool pecj_registered = []() {
        PluginRegistry::instance().register_plugin(
            "pecj",
            [](const PluginConfig& cfg) -> PluginPtr {
                return std::make_shared<PECJAdapter>(cfg);
            }
        );
        return true;
    }();
}
```

### 8.2 插件创建流程
```cpp
// 用户代码
PluginManager plugin_mgr;
plugin_mgr.loadPlugin("pecj", config);

// 内部实现
bool PluginManager::loadPlugin(const std::string& name, const PluginConfig& config) {
    // 1. 通过 Registry 查找工厂函数
    auto plugin = PluginRegistry::instance().create_plugin(name, config);
    
    // 2. 调用插件的 initialize() 方法
    if (plugin->initialize(config)) {
        // 3. 存储插件实例
        plugins_[name] = plugin;
        return true;
    }
    return false;
}
```

### 8.3 添加新插件的步骤
1.  **创建 Adapter 类**：继承 `IAlgorithmPlugin`
2.  **实现接口方法**：`initialize()`, `feedData()`, `process()`, `getStats()` 等
3.  **注册工厂函数**：在 `.cpp` 文件中添加全局静态注册代码
4.  **更新 CMakeLists.txt**：将新源文件加入 `sage_tsdb_plugins` 库

示例：
```cpp
// my_custom_adapter.cpp
#include "my_custom_adapter.h"
#include "sage_tsdb/plugins/plugin_registry.h"

namespace {
    bool custom_registered = []() {
        PluginRegistry::instance().register_plugin(
            "my_custom",
            [](const PluginConfig& cfg) -> PluginPtr {
                return std::make_shared<MyCustomAdapter>(cfg);
            }
        );
        return true;
    }();
}

// 实现类方法...
```

## 9. 架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           sageTSDB System                                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────┐     ┌──────────────────────────────────────────────────┐ │
│  │   External   │     │              PluginManager                        │ │
│  │  Data Source │────▶│  ┌─────────────────────────────────────────────┐ │ │
│  │ (CSV/Kafka)  │     │  │            EventBus                          │ │ │
│  └──────────────┘     │  │  (worker_thread_, event_queue_)              │ │ │
│                       │  │  Pub/Sub with Zero-Copy shared_ptr           │ │ │
│                       │  └────────┬──────────────────────┬──────────────┘ │ │
│                       │           │  EVENT_TYPES:        │                │ │
│                       │           │  - DATA_INGESTED     │                │ │
│                       │           │  - WINDOW_TRIGGERED  │                │ │
│                       │           │  - RESULT_READY      │                │ │
│                       │           │  - ERROR_OCCURRED    │                │ │
│                       │           ▼                      ▼                │ │
│                       │  ┌─────────────────┐     ┌──────────────────┐    │ │
│                       │  │  PECJAdapter    │     │ FaultDetection   │    │ │
│                       │  │  ┌────────────┐ │     │    Adapter       │    │ │
│                       │  │  │data_queue_ │ │     │  ┌─────────────┐ │    │ │
│                       │  │  │(S/R stream)│ │     │  │value_history│ │    │ │
│                       │  │  └─────┬──────┘ │     │  │(deque)      │ │    │ │
│                       │  │        ▼        │     │  └──────┬──────┘ │    │ │
│                       │  │  worker_thread_ │     │         ▼        │    │ │
│                       │  │        ▼        │     │   detectZScore() │    │ │
│                       │  │ convertToTuple  │     │   detectVAE()    │    │ │
│                       │  │        ▼        │     │         ▼        │    │ │
│                       │  │  ┌──────────┐  │     │  DetectionResult │    │ │
│                       │  │  │OoOJoin:: │  │     │         ▼        │    │ │
│                       │  │  │Abstract  │  │     │  publish(ERROR_) │    │ │
│                       │  │  │Operator  │  │     └──────────────────┘    │ │
│                       │  │  │(PECJ Lib)│  │                             │ │
│                       │  │  └────┬─────┘  │                             │ │
│                       │  │       │        │                             │ │
│                       │  │  ┌────▼─────┐  │  PluginRegistry (Singleton) │ │
│                       │  │  │getResult()│  │  ┌───────────────────────┐ │ │
│                       │  │  │getAQP()  │  │  │ Factory Functions:    │ │ │
│                       │  │  └──────────┘  │  │ - "pecj" → PECJAdapter│ │ │
│                       │  └─────────────────┘  │ - "fault_detection" → │ │ │
│                       │                       │   FaultDetection...   │ │ │
│                       └───────────────────────┴───────────────────────┘ │ │
│                                                                          │ │
│  ┌────────────────────────────────────────────────────────────────────┐ │ │
│  │                      Data Flow (Zero-Copy)                          │ │ │
│  │  shared_ptr<TimeSeriesData> ──▶ feedDataToAll()                    │ │ │
│  │       │                                │                            │ │ │
│  │       ├────▶ PECJAdapter::feedData()   │                            │ │ │
│  │       │           ▼                     │                            │ │ │
│  │       │     data_queue_.push()         │                            │ │ │
│  │       │           ▼                     │                            │ │ │
│  │       │     workerLoop() ──▶ convertToTrackTuple()                  │ │ │
│  │       │           ▼                     │                            │ │ │
│  │       │     feedTupleS() / feedTupleR() │                            │ │ │
│  │       │                                 │                            │ │ │
│  │       └────▶ FaultDetectionAdapter::feedData()                      │ │ │
│  │                     ▼                                                │ │ │
│  │             value_history_.push_back()                               │ │ │
│  └────────────────────────────────────────────────────────────────────┘ │ │
│                                                                          │ │
└─────────────────────────────────────────────────────────────────────────┘ │
                                                                              
Window Visibility and Interaction:
┌─────────────────────────────────────────────────────────────────────────┐
│  S Stream: [t1, t2, t3, ...]                                            │
│  R Stream: [t1', t2', t3', ...]                                         │
│            │                                                             │
│            ▼ feedTupleS/R                                                │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │  PECJ Internal Window [T, T+windowLen]                      │        │
│  │  - S Buffer (size: sLen)                                    │        │
│  │  - R Buffer (size: rLen)                                    │        │
│  │  - Watermark: T + latenessMs                                │        │
│  └─────────────────────┬───────────────────────────────────────┘        │
│                        ▼ Watermark Trigger                              │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │  Join Result: {(s_i, r_j) | key_match && in_window}        │        │
│  │  - Exact Count: getResult()                                 │        │
│  │  - AQP Estimate: getApproximateResult()                     │        │
│  └─────────────────────┬───────────────────────────────────────┘        │
│                        ▼ publishWindowResult()                          │
│  ┌─────────────────────────────────────────────────────────────┐        │
│  │  EventBus: WINDOW_TRIGGERED / RESULT_READY                  │        │
│  │  Payload: {join_count, aqp_result, window_id}               │        │
│  └─────────────────────┬───────────────────────────────────────┘        │
│                        ▼ Event Subscribers                              │
│         ┌──────────────┴──────────────┐                                 │
│         ▼                             ▼                                 │
│  FaultDetection                  Custom Listeners                       │
│  (Process join results)          (Logging, Metrics)                     │
└─────────────────────────────────────────────────────────────────────────┘
```

## 10. 支持的 PECJ 算子类型

| 算子名称 | 枚举值 | 描述 | 适用场景 | 支持 AQP |
|---------|--------|------|---------|----------|
| IAWJ | `OperatorType::IAWJ` | Interval-Aware Window Join | 基础窗口连接，精确结果 | ❌ |
| IMA | `OperatorType::IMA` | IMA-based AQP | 近似查询处理，支持乱序，推荐用于高吞吐场景 | ✅ |
| MSWJ | `OperatorType::MSWJ` | Multi-Stream Window Join | 多流连接（>2 个流） | ❌ |
| AI | `OperatorType::AI` | AI-enhanced Operator | 机器学习增强的 Join | ✅ |
| LinearSVI | `OperatorType::LINEAR_SVI` | Linear Stochastic Variational Inference | 变分推断预测，适合概率查询 | ✅ |
| MeanAQP | `OperatorType::MEAN_AQP` | Mean-based AQP | 均值近似，计算快速 | ✅ |
| SHJ | `OperatorType::SHJ` | Symmetric Hash Join | 对称哈希连接，经典算法 | ❌ |
| PRJ | `OperatorType::PRJ` | Partitioned Range Join | 分区范围连接，适合范围谓词 | ❌ |

**算子选择建议**:
*   **生产环境（高吞吐）**: `IMA` 或 `MeanAQP`（支持近似结果，性能最佳）
*   **精确结果需求**: `SHJ` 或 `IAWJ`
*   **研究/实验**: `LinearSVI` 或 `AI`（支持概率建模）
*   **多流场景**: `MSWJ`

**配置方式**:
```cpp
// 方式1：通过配置字符串
PluginConfig config = {{"operator", "IMA"}};

// 方式2：通过枚举设置（需要类型转换）
auto adapter = std::dynamic_pointer_cast<PECJAdapter>(plugin);
adapter->setOperatorType(PECJAdapter::OperatorType::IMA);
```

## 11. 性能监控与统计

### 11.1 PECJ 适配器统计项
`PECJAdapter::getStats()` 返回以下指标（通过 `std::atomic` 实现线程安全）：

```cpp
{
    "tuples_processed_s": 5000,      // S流处理的元组数
    "tuples_processed_r": 5000,      // R流处理的元组数
    "join_results": 2500,            // Join结果总数
    "total_latency_us": 12345,       // 总处理延迟（微秒）
    "avg_latency_us": 1.23,          // 平均延迟 = total / (s+r)
    "throughput_ktps": 800.5         // 吞吐量（千元组/秒）
}
```

### 11.2 故障检测适配器统计项
`FaultDetectionAdapter::getStats()` 返回：

```cpp
{
    "total_samples": 10000,          // 总样本数
    "anomalies_detected": 25,        // 检测到的异常数
    "detection_rate": 0.0025,        // 异常率 = anomalies / total
    "false_positives": 3,            // 假阳性（如果有标签）
    "true_positives": 22,            // 真阳性
    "current_threshold": 3.0         // 当前阈值
}
```

### 11.3 获取实时统计

```cpp
// 方式1：获取所有插件统计
auto all_stats = plugin_mgr.getAllStats();
for (const auto& [plugin_name, stats] : all_stats) {
    std::cout << "Plugin: " << plugin_name << std::endl;
    for (const auto& [key, value] : stats) {
        std::cout << "  " << key << ": " << value << std::endl;
    }
}

// 方式2：获取特定插件的详细统计
auto pecj = std::dynamic_pointer_cast<PECJAdapter>(
    plugin_mgr.getPlugin("pecj"));
if (pecj) {
    auto time_breakdown = pecj->getTimeBreakdown();
    // time_breakdown 可能包含：
    // {"join_time_us", "inference_time_us", "io_time_us"}
}

// 方式3：获取故障检测历史
auto fd = std::dynamic_pointer_cast<FaultDetectionAdapter>(
    plugin_mgr.getPlugin("fault_detection"));
if (fd) {
    auto results = fd->getDetectionResults(10);  // 最近10条
    for (const auto& result : results) {
        if (result.is_anomaly) {
            std::cout << "Anomaly at " << result.timestamp
                     << " score=" << result.anomaly_score << std::endl;
        }
    }
}
```

### 11.4 监控最佳实践

1.  **定期轮询**: 在主循环中每隔 1-5 秒调用 `getAllStats()`
2.  **导出到监控系统**: 将统计指标发送到 Prometheus/Grafana
3.  **设置告警**: 基于异常率、延迟等指标设置阈值告警
4.  **日志记录**: 使用 EventBus 订阅 `ERROR_OCCURRED` 事件记录异常

```cpp
// 示例：导出到 Prometheus
auto stats = plugin_mgr.getAllStats();
for (const auto& [plugin, metrics] : stats) {
    for (const auto& [name, value] : metrics) {
        prometheus_gauge.Set({{"plugin", plugin}, {"metric", name}}, value);
    }
}
```

## 12. 故障排查与常见问题

### 12.1 编译问题

**问题1：找不到 PyTorch 头文件**
```
fatal error: torch/torch.h: No such file or directory
```
**解决方案**:
```bash
# 检查 PyTorch 安装
python3 -c "import torch; print(torch.__version__)"

# 手动设置 CMAKE_PREFIX_PATH
CMAKE_PREFIX_PATH=$(python3 -c "import torch; print(torch.utils.cmake_prefix_path)")
cmake -DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH -DPECJ_FULL_INTEGRATION=ON ..
```

**问题2：ABI 不兼容错误**
```
undefined reference to `std::__cxx11::basic_string...`
```
**解决方案**: 确保使用旧版 ABI
```cmake
# CMakeLists.txt 中已包含
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
```

### 12.2 运行时问题

**问题3：插件加载失败**
```
Failed to create plugin 'pecj'
```
**排查步骤**:
1.  检查插件是否注册：`PluginRegistry::instance().list_plugins()`
2.  检查配置是否正确：验证 `operator` 字段是否有效
3.  查看日志：是否有初始化错误信息

**问题4：数据未被处理**
```
tuples_processed_s = 0, tuples_processed_r = 0
```
**排查步骤**:
1.  检查插件是否启动：`plugin_mgr.startAll()` 返回 true
2.  检查数据流标识：`tags["stream"]` 是否设置为 "S" 或 "R"
3.  检查工作线程：是否卡在队列等待中（可能是死锁）

**问题5：Join 结果为 0**
```
join_results = 0
```
**可能原因**:
1.  窗口配置错误：`windowLen` 太小，无法容纳匹配
2.  Key 不匹配：检查 `tags["key"]` 或哈希函数
3.  时间戳错误：`eventTime` 不在窗口范围内
4.  Stub 模式：未启用 `PECJ_FULL_INTEGRATION`

**调试技巧**:
```cpp
// 添加详细日志
#define DEBUG_PECJ
// 在 feedData 中打印
std::cout << "Feed: ts=" << data.timestamp 
          << " stream=" << (is_s ? "S" : "R")
          << " key=" << data.tags["key"] << std::endl;
```

### 12.3 性能问题

**问题6：吞吐量低**
**优化建议**:
1.  增加 PECJ 内部线程数：`{"threads", "8"}`
2.  增大缓冲区：`{"sLen", "50000"}, {"rLen", "50000"}`
3.  启用零拷贝：`res_config.enable_zero_copy = true`
4.  使用 AQP 算子：`IMA` 或 `MeanAQP` 代替精确算子

**问题7：内存占用过高**
**排查方案**:
1.  检查历史窗口大小：减小 `window_size`
2.  限制事件队列：减小 `event_queue_size`
3.  清理检测历史：调用 `reset()` 或减小 `max_history_size_`

### 12.4 集成演示问题

**问题8：找不到数据集文件**
```
[ERROR] Failed to open file: sTuple.csv
```
**解决方案**:
```bash
# 使用绝对路径或确保数据集存在
./integrated_demo \
    --s-file /absolute/path/to/PECJ/benchmark/datasets/sTuple.csv \
    --r-file /absolute/path/to/PECJ/benchmark/datasets/rTuple.csv
```

**问题9：Segmentation Fault**
**可能原因**:
1.  Stub 模式下访问了 PECJ 指针（应有 `#ifdef` 保护）
2.  多线程竞争条件（检查 mutex 使用）
3.  插件未正确初始化就调用 `feedData`

**调试方法**:
```bash
# 使用 GDB
gdb ./integrated_demo
run --s-file ... --r-file ...
# 崩溃后
bt  # 查看堆栈跟踪
```

### 12.5 获取帮助

*   **查看文档**: `docs/` 目录下的所有 `.md` 文件
*   **运行示例**: `examples/` 目录提供完整的工作示例
*   **查看测试**: `tests/` 目录展示单元测试用法
*   **GitHub Issues**: 提交问题到仓库的 Issues 页面

## 13. 设计原则总结

### 13.1 解耦设计
*   **插件与核心分离**: PECJ 和 sageTSDB 通过适配器模式完全解耦，PECJ 无需了解 sageTSDB 内部结构。
*   **接口标准化**: 所有插件实现统一的 `IAlgorithmPlugin` 接口，易于扩展。
*   **依赖隔离**: 通过 `#ifdef PECJ_FULL_INTEGRATION` 条件编译，在 Stub 模式下完全不依赖 PECJ 库。

### 13.2 性能优化
*   **零拷贝传递**: 使用 `std::shared_ptr` 在插件间共享数据，避免内存拷贝。
*   **异步处理**: 每个插件维护独立的工作线程和数据队列，避免阻塞主线程。
*   **锁粒度优化**: 使用 `std::atomic` 记录统计信息，仅在必要时使用 mutex。

### 13.3 可移植性
*   **平台无关**: 仅依赖标准 C++20 和 POSIX API（`gettimeofday` 可替换）。
*   **数据库无关**: `PECJAdapter` 可轻松移植到其他数据库系统（如 PostgreSQL, InfluxDB）。
*   **构建灵活性**: 支持 Stub 模式和 Full 模式，适应不同部署环境。

### 13.4 可扩展性
*   **插件注册机制**: 通过工厂模式动态注册新插件，无需修改核心代码。
*   **事件驱动架构**: EventBus 支持自定义事件类型和订阅者，易于添加新的交互逻辑。
*   **配置驱动**: 所有参数通过 `PluginConfig` 传递，支持运行时调整。

## 14. 未来工作与路线图

### 14.1 短期计划（已完成或进行中）
- [x] 基础插件架构实现
- [x] PECJ 适配器（Stub + Full 模式）
- [x] 故障检测适配器（Z-Score + VAE）
- [x] EventBus 事件系统
- [x] 集成演示程序
- [ ] Python 绑定（通过 pybind11）
- [ ] 性能基准测试套件
- [ ] CI/CD 自动化测试

### 14.2 中期计划
- [ ] **持久化支持**: 集成 LSM-Tree 存储引擎，支持窗口状态快照
- [ ] **分布式扩展**: 支持多节点部署，通过 gRPC 或 Kafka 分发数据
- [ ] **更多算法插件**: 
    - 时间序列预测（ARIMA, LSTM）
    - 模式挖掘（Frequent Patterns）
    - 聚类分析（K-Means, DBSCAN）
- [ ] **监控面板**: 基于 Web 的实时监控界面（WebSocket + React）
- [ ] **自适应调优**: 基于负载自动调整窗口大小、缓冲区等参数

### 14.3 长期愿景
- [ ] **SQL 查询接口**: 支持声明式流查询（类似 Apache Flink SQL）
- [ ] **机器学习管道**: 与 MLflow 集成，支持模型训练、版本管理
- [ ] **边缘计算**: 轻量级部署到 IoT 设备（ARM 架构）
- [ ] **云原生**: Kubernetes Operator，支持自动伸缩和故障恢复

## 15. 相关文档

*   **[LSM_TREE_IMPLEMENTATION.md](LSM_TREE_IMPLEMENTATION.md)**: LSM-Tree 存储引擎实现细节
*   **[PERSISTENCE.md](PERSISTENCE.md)**: 持久化机制设计
*   **[QUICKSTART.md](../examples/QUICKSTART.md)**: 快速开始指南
*   **[DEMO_SUMMARY.md](../examples/DEMO_SUMMARY.md)**: 演示程序说明
*   **PECJ 论文**: [链接到原始论文或仓库]

## 16. 贡献指南

欢迎贡献代码、文档和问题反馈！

**贡献流程**:
1.  Fork 本仓库
2.  创建特性分支：`git checkout -b feature/my-new-feature`
3.  遵循代码风格（使用 `clang-format`）
4.  添加单元测试（覆盖率 > 80%）
5.  提交 Pull Request

**代码规范**:
*   使用 C++20 现代特性（`std::ranges`, `concepts` 等）
*   注释使用 Doxygen 格式
*   变量命名：`snake_case` for variables, `PascalCase` for types
*   避免裸指针，优先使用智能指针

**联系方式**:
*   Email: [维护者邮箱]
*   Slack/Discord: [社区链接]

---

**文档版本**: v1.0  
**最后更新**: 2024-12-02  
**维护者**: sageTSDB Team
