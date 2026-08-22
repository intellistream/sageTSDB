# sageTSDB 计算层接口文档 v1.0

**适用范围**：企业级数据库集成方，仅使用 sageTSDB 计算组件（`sage_tsdb_pecj_engine`），不涉及存储层实现。

**最后更新**：2026-08-13

---

## 目录

1. [概览](#1-概览)
2. [需要实现的接口](#2-需要实现的接口)
3. [核心数据结构](#3-核心数据结构)
4. [PECJComputeEngine API](#4-pecjcomputeengine-api)
5. [WindowScheduler API](#5-windowscheduler-api)
6. [ComputeStateManager API](#6-computestatemanager-api可选)
7. [完整调用流程](#7-完整调用流程)
8. [编译宏说明](#8-编译宏说明)
9. [已知注意事项](#9-已知注意事项)

---

## 1. 概览

sageTSDB 计算层由三个组件构成，它们的职责和依赖关系如下：

```
调用方（企业DB侧）
      │
      │ 1. 写入数据后调用 onDataInserted()
      ▼
┌─────────────────────┐     2. 触发窗口      ┌────────────────────────┐
│   WindowScheduler   │ ─────────────────▶  │  PECJComputeEngine     │
│  (窗口调度、水位线)  │                     │  (调用PECJ算子完成连接)  │
└─────────────────────┘                     └───────────┬────────────┘
      │                                                 │
      │ 3. submitTask() 异步执行                         │ 4. query() 取数据
      ▼                                                 ▼
ResourceHandle（由调用方实现）             TimeSeriesDB（由调用方提供）
```

**调用方必须提供：**
- 一个实现了 `core::ResourceHandle` 的线程池适配器
- 一个 `TimeSeriesDB` 实例（内部指向企业DB的存储后端）

---

## 2. 需要实现的接口

### 2.1 `core::ResourceHandle`（必须实现）

**文件**：`include/sage_tsdb/core/resource_manager.h`

`WindowScheduler` 通过此接口异步提交窗口计算任务，**只有 `submitTask()` 是实际使用的**，其余方法返回占位值即可。

```cpp
class core::ResourceHandle {
public:
    // ★ 唯一被实际调用的方法
    // 将一个无参可调用对象提交到线程池异步执行
    // 返回：提交成功返回 true，队列满/关闭返回 false
    virtual bool submitTask(std::function<void()> task) = 0;

    // 以下三个方法在当前实现中未被调用，返回占位值即可
    virtual bool isValid() const = 0;
    virtual ResourceRequest getAllocated() const = 0;
    virtual void reportUsage(const ResourceUsage& usage) = 0;
};
```

**最小实现示例：**

```cpp
class EnterpriseResourceHandle : public sage_tsdb::core::ResourceHandle {
    EnterpriseThreadPool& pool_;
public:
    explicit EnterpriseResourceHandle(EnterpriseThreadPool& pool) : pool_(pool) {}

    bool submitTask(std::function<void()> task) override {
        pool_.submit(std::move(task));
        return true;
    }
    bool isValid() const override { return true; }
    sage_tsdb::core::ResourceRequest getAllocated() const override { return {}; }
    void reportUsage(const sage_tsdb::core::ResourceUsage&) override {}
};
```

---

### 2.2 `TimeSeriesDB`（由 sageTSDB 提供，后端替换）

`PECJComputeEngine` 持有 `TimeSeriesDB*`，在每次窗口计算时调用：

```cpp
// 唯一被实际调用的两个查询（pecj_compute_engine.cpp 第283-284行）
db->query("stream_s", query_range);  // 查询 S 流数据
db->query("stream_r", query_range);  // 查询 R 流数据
```

调用方需要在初始化时建好含数据的 `TimeSeriesDB`，并确保：
- 存在名为 `stream_s` 和 `stream_r` 的表（名称可通过 `ComputeConfig` 配置）
- 表中数据的格式满足 [§3.4 输入数据格式约定](#34-输入数据格式约定)

---

## 3. 核心数据结构

### 3.1 `TimeRange`

两个命名空间中的 `TimeRange` 现已统一字段名，均使用 `start_us` / `end_us`，单位均为微秒。

**`sage_tsdb::compute::TimeRange`**（计算层，用于窗口计算）  
**文件**：`include/sage_tsdb/compute/pecj_compute_engine.h`

```cpp
namespace sage_tsdb::compute {
struct TimeRange {
    int64_t start_us;  // 窗口起始时间戳，单位：微秒（μs），左闭
    int64_t end_us;    // 窗口结束时间戳，单位：微秒（μs），左开

    int64_t duration() const;              // end_us - start_us
    bool    contains(int64_t ts) const;    // ts >= start_us && ts < end_us
    bool    valid() const;                 // end_us > start_us
};
}
```

**`sage_tsdb::TimeRange`**（core 层，用于数据库查询）  
**文件**：`include/sage_tsdb/core/time_series_data.h`

```cpp
namespace sage_tsdb {
struct TimeRange {
    int64_t start_us;  // 查询起始时间戳，单位：微秒（μs），左闭
    int64_t end_us;    // 查询结束时间戳，单位：微秒（μs），右闭

    int64_t duration() const;              // end_us - start_us
    bool    contains(int64_t ts) const;    // ts >= start_us && ts <= end_us
};
}
```

> 两者字段名相同，唯一区别是边界语义：compute 层左闭右开（窗口），core 层左闭右闭（查询范围）。

### 3.2 `TimeSeriesData`（core 层，流数据的存储格式）

**文件**：`include/sage_tsdb/core/time_series_data.h`

```cpp
struct TimeSeriesData {
    int64_t        timestamp;  // 时间戳，单位：毫秒（ms）
    TimeSeriesValue value;     // double 或 vector<double>
    Tags            tags;      // map<string,string>，用于存放 join key
    Fields          fields;    // map<string,string>，用于存放 payload
};

using Tags   = std::map<std::string, std::string>;
using Fields = std::map<std::string, std::string>;
```

### 3.3 `ComputeConfig`（算法配置）

**文件**：`include/sage_tsdb/compute/pecj_compute_engine.h`

```cpp
struct ComputeConfig {
    // ── 窗口参数 ──────────────────────────────────────────────────
    uint64_t    window_len_us    = 1'000'000;  // 窗口长度，默认 1s（μs）
    uint64_t    slide_len_us     = 500'000;    // 滑动步长，默认 500ms（μs）

    // ── 算子选择 ──────────────────────────────────────────────────
    std::string operator_type    = "IAWJ";     // 算子标签字符串，见下表
    PECJOperatorType operator_enum = PECJOperatorType::IAWJ;

    // ── 算子参数 ──────────────────────────────────────────────────
    uint64_t    max_delay_us     = 100'000;    // 最大乱序延迟，默认 100ms（μs）
    double      aqp_threshold    = 0.05;       // AQP 误差上界，默认 5%
    uint64_t    s_buffer_len     = 100'000;    // S 流算子内部缓冲大小（条数）
    uint64_t    r_buffer_len     = 100'000;    // R 流算子内部缓冲大小（条数）
    uint64_t    time_step_us     = 1'000;      // 算子内部时间步长，默认 1ms（μs）
    std::string watermark_tag    = "arrival";  // 水位线策略："arrival" 或 "lateness"
    uint64_t    watermark_time_ms = 100;       // ArrivalWM 触发间隔（ms）
    uint64_t    lateness_ms      = 50;         // LatenessWM 最大乱序容忍（ms）

    // ── 结果模式 ──────────────────────────────────────────────────
    bool join_sum = false;   // false → 返回匹配对数（Join Count）
                             // true  → 返回 count × avg_value（Join Sum）

    // ── 算子特定开关 ──────────────────────────────────────────────
    bool ima_disable_compensation = false;  // IMA：禁用补偿（退化为纯 Eager join）
    bool mswj_compensation        = false;  // MSWJ：启用线性补偿

    // ── 资源限制 ──────────────────────────────────────────────────
    size_t max_memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 内存上限，默认 2GB
    int    max_threads      = 4;                           // 线程数上限

    // ── 性能调优 ──────────────────────────────────────────────────
    bool     enable_aqp  = true;   // 启用 AQP 回退
    bool     enable_simd = true;   // 启用 SIMD 优化
    uint64_t timeout_ms  = 1'000;  // 单窗口计算超时（ms）

    // ── 表名（与 TimeSeriesDB 中的表名对应）──────────────────────
    std::string stream_s_table = "stream_s";
    std::string stream_r_table = "stream_r";
    std::string result_table   = "join_results";  // 当前写回功能尚未实现
};
```

**算子类型对照表：**

| `operator_type` 字符串 | `PECJOperatorType` 枚举 | 说明 | 支持 AQP |
|---|---|---|:---:|
| `"IAWJ"` | `IAWJ` | 窗口内连接，单窗口，最简基线 | ✗ |
| `"IMA"` | `IMA` | 增量移动均值 IAWJ，EAGER join | ✓ |
| `"PECJ"` | `PECJ` | 完整 PECJ（内部使用 IMA） | ✓ |
| `"MeanAQP"` | `MeanAQP` | 指数加权均值 AQP 预测 | ✓ |
| `"MSWJ"` | `MSWJ` | 多流窗口连接（ICDE 2016） | ✓ |
| `"IAWJSel"` | `IAWJSel` | IAWJ + 选择率 AQP（粗粒度） | ✓ |
| `"LazyIAWJSel"` | `LazyIAWJSel` | 延迟计算版 IAWJSel | ✓ |
| `"AI"` | `AI` | AI 增强算子 | ✗ |
| `"LinearSVI"` | `LinearSVI` | 线性随机变分推断 | ✗ |
| `"SHJ"` | `SHJ` | 对称哈希连接（原始基线） | ✗ |
| `"PRJ"` | `PRJ` | 渐进连接（原始基线） | ✗ |

---

### 3.4 输入数据格式约定

**依据**：`src/compute/pecj_compute_engine.cpp` 第 342–378 行

写入 `stream_s` / `stream_r` 表的每条 `TimeSeriesData` 必须按如下约定填充：

```cpp
TimeSeriesData record;
record.timestamp       = event_time_us;           // 事件时间，单位：微秒（μs）
record.tags["key"]     = std::to_string(key);     // ★ JOIN 键，字符串形式的 uint64_t
                                                  //   引擎用 stoull() 转换；缺失时默认 0
record.fields["value"] = std::to_string(payload); // ★ 载荷，字符串形式的 double
                                                  //   引擎用 stod() 转换；缺失时默认 0
// record.value 字段在 compute 层不参与连接计算，可设任意值
```

---

### 3.5 `ComputeStatus`（单窗口计算结果）

```cpp
struct ComputeStatus {
    bool        success            = false;  // 计算是否成功
    std::string error;                       // 失败时的错误信息

    uint64_t    window_id          = 0;      // 对应的窗口 ID
    size_t      join_count         = 0;      // 精确连接对数（join_sum=true 时为聚合值）
    double      aqp_estimate       = 0.0;   // AQP 估计值（仅支持 AQP 的算子有效）

    double      computation_time_ms = 0.0;  // 本次窗口计算耗时（ms）
    size_t      input_s_count      = 0;     // S 流输入条数
    size_t      input_r_count      = 0;     // R 流输入条数
    size_t      memory_used_bytes  = 0;     // 本次计算内存消耗

    double      selectivity        = 0.0;   // join_count / (|S| × |R|)
    double      aqp_error          = 0.0;   // |精确值 - AQP 估计| / 精确值
    bool        used_aqp           = false; // 本次是否使用了 AQP
    bool        timeout_occurred   = false; // 是否发生超时
};
```

### 3.6 `ComputeMetrics`（累计运行指标）

```cpp
struct ComputeMetrics {
    uint64_t total_windows_completed       = 0;
    uint64_t total_tuples_processed        = 0;
    double   avg_throughput_events_per_sec = 0.0;

    double   avg_window_latency_ms         = 0.0;
    double   min_window_latency_ms         = 0.0;
    double   max_window_latency_ms         = 0.0;
    double   p99_window_latency_ms         = 0.0;

    size_t   peak_memory_bytes             = 0;
    size_t   avg_memory_bytes              = 0;
    int      active_threads                = 0;

    double   avg_join_selectivity          = 0.0;
    double   avg_aqp_error_rate            = 0.0;
    uint64_t aqp_invocations               = 0;

    uint64_t failed_windows                = 0;
    uint64_t timeout_windows               = 0;
    uint64_t retry_count                   = 0;
};
```

---

## 4. `PECJComputeEngine` API

**文件**：`include/sage_tsdb/compute/pecj_compute_engine.h`  
**命名空间**：`sage_tsdb::compute`  
**编译条件**：`PECJ_MODE_INTEGRATED`

### 4.1 构造与析构

```cpp
PECJComputeEngine();
~PECJComputeEngine();

// 不可拷贝，不可移动
```

### 4.2 `initialize()`

```cpp
bool initialize(const ComputeConfig&  config,
                TimeSeriesDB*         db,
                core::ResourceHandle* resource_handle);
```

| 参数 | 类型 | 说明 |
|---|---|---|
| `config` | `const ComputeConfig&` | 算法配置，见 §3.3 |
| `db` | `TimeSeriesDB*` | 数据库指针，**不持有所有权**；引擎将调用 `db->query(stream_s_table, ...)` 和 `db->query(stream_r_table, ...)` |
| `resource_handle` | `core::ResourceHandle*` | **当前实现中未被使用**，可传 `nullptr`；`WindowScheduler` 才是实际使用方 |

**返回**：`true` 初始化成功；`false` 已初始化过、参数无效或算子创建失败。

**约束**：`db` 不得为 `nullptr`；`window_len_us` 和 `slide_len_us` 均不得为 0。

---

### 4.3 `executeWindowJoin()`

```cpp
ComputeStatus executeWindowJoin(uint64_t         window_id,
                                const TimeRange& time_range);
```

这是计算引擎的核心方法，通常由 `WindowScheduler` 自动调用，也可手动调用。

| 参数 | 类型 | 说明 |
|---|---|---|
| `window_id` | `uint64_t` | 窗口唯一标识符，写入结果时使用 |
| `time_range` | `const compute::TimeRange&` | 窗口时间区间，**单位微秒**，左闭右开 `[start_us, end_us)` |

**执行流程（实现内部）：**

1. 将 `time_range` 转换为 `sage_tsdb::TimeRange`（字段映射：`start_us → start_time`，`end_us → end_time`）
2. 调用 `db->query(stream_s_table, ...)` 和 `db->query(stream_r_table, ...)` 取数据
3. 对所有时间戳做归一化（减去最小时间戳），使 PECJ 算子在 `[0, window_len]` 内工作
4. 调用 `pecj_operator_->start()` 重置算子状态
5. 依次调用 `feedTupleS()` / `feedTupleR()` 喂入数据
6. 调用 `getResult()` 和 `getAQPResult()` 取连接结果
7. 调用 `pecj_operator_->stop()`
8. 更新内部 `ComputeMetrics`

**返回**：`ComputeStatus`，详见 §3.5。

**线程安全**：不保证，`WindowScheduler` 通过 `ResourceHandle::submitTask()` 串行化调用。

---

### 4.4 `getMetrics()`

```cpp
ComputeMetrics getMetrics() const;
```

线程安全（内部使用 `shared_mutex`）。返回自 `initialize()` 后的累计指标快照，详见 §3.6。

---

### 4.5 `reset()`

```cpp
void reset();
```

清除内部 `ComputeMetrics` 和延迟历史样本。**不清除** `TimeSeriesDB` 中的数据，**不重置** PECJ 算子本身。

---

### 4.6 其他

```cpp
bool                 isInitialized() const;  // 是否已初始化
const ComputeConfig& getConfig() const;      // 返回当前配置（只读）
```

---

## 5. `WindowScheduler` API

**文件**：`include/sage_tsdb/compute/window_scheduler.h`  
**命名空间**：`sage_tsdb::compute`  
**编译条件**：`PECJ_MODE_INTEGRATED`

### 5.1 构造

```cpp
WindowScheduler(const WindowSchedulerConfig& config,
                PECJComputeEngine*            compute_engine,
                core::TableManager*           table_manager,
                core::ResourceHandle*         resource_handle);
```

| 参数 | 类型 | 说明 |
|---|---|---|
| `config` | `const WindowSchedulerConfig&` | 调度配置，见 §5.2 |
| `compute_engine` | `PECJComputeEngine*` | 已初始化的计算引擎，**不持有所有权** |
| `table_manager` | `core::TableManager*` | ⚠️ **当前实现中从未调用**，但构造函数做了非空检查；传入任意有效指针或创建空的 `TableManager` 实例即可 |
| `resource_handle` | `core::ResourceHandle*` | ★ **实际使用**，窗口计算通过 `submitTask()` 异步执行；不得为 `nullptr` |

**约束**：三个指针均不得为 `nullptr`（否则抛 `std::invalid_argument`）。

---

### 5.2 `WindowSchedulerConfig`

```cpp
struct WindowSchedulerConfig {
    WindowType    window_type       = WindowType::Sliding;
    uint64_t      window_len_us     = 1'000'000;  // 窗口长度（μs）
    uint64_t      slide_len_us      = 500'000;    // 滑动步长（μs）
    JoinSemantics join_semantics    = JoinSemantics::Eager;

    TriggerPolicy trigger_policy    = TriggerPolicy::Hybrid;
    uint64_t      trigger_interval_us     = 100'000;  // 定时检查间隔（μs）
    size_t        trigger_count_threshold = 1'000;    // CountBased 触发阈值（条数）

    uint64_t      max_delay_us      = 100'000;    // 最大乱序延迟（μs）
    uint64_t      watermark_slack_us = 50'000;    // 水位线松弛量（μs）
    bool          allow_late_data   = true;       // 是否处理迟到数据

    size_t        max_pending_windows     = 10;   // 最大等待窗口数
    size_t        max_concurrent_windows  = 4;    // 最大并发计算窗口数
    bool          enable_adaptive_scheduling = true;

    std::string   stream_s_table    = "stream_s"; // 与 ComputeConfig 保持一致
    std::string   stream_r_table    = "stream_r";

    bool          enable_metrics    = true;
    uint64_t      metrics_report_interval_us = 1'000'000;  // 指标上报间隔（μs）
};
```

**枚举取值：**

```
WindowType:    Tumbling | Sliding | Session | IntraWindow | MultiStream
TriggerPolicy: TimeBased | CountBased | Hybrid | Watermark | Manual
JoinSemantics: Eager | Lazy | AQP
```

---

### 5.3 生命周期

```cpp
bool start();                          // 启动后台调度线程，返回 true 表示成功
void stop(bool wait_completion = true); // 停止调度；wait_completion=true 时等待所有
                                       //   活跃窗口计算完成再退出
bool isRunning() const;                // 当前是否处于运行状态
```

---

### 5.4 通知接口（调用方主动调用）

#### `onDataInserted()`

```cpp
void onDataInserted(const std::string& table_name,
                    int64_t            timestamp,
                    size_t             count = 1);
```

每次向 `stream_s` 或 `stream_r` 表写入数据后，**必须**调用此方法通知调度器。

| 参数 | 类型 | 说明 |
|---|---|---|
| `table_name` | `const std::string&` | 被写入的表名，如 `"stream_s"` |
| `timestamp` | `int64_t` | 本次写入数据的时间戳，单位**微秒** |
| `count` | `size_t` | 本次写入的条数，默认为 1 |

**内部行为**：自动推进水位线，判断窗口是否触发，若满足条件则放入待计算队列。

---

#### `watchTable()`

```cpp
void watchTable(const std::string& table_name, int stream_id);
```

注册需要监听的表，在 `start()` 之前调用。

| 参数 | 说明 |
|---|---|
| `table_name` | 要监听的表名 |
| `stream_id` | `0` 表示 S 流，`1` 表示 R 流 |

---

### 5.5 手动触发

```cpp
// 手动调度指定时间范围的窗口
bool scheduleWindow(uint64_t window_id, const TimeRange& time_range);

// 强制触发所有待处理窗口，返回触发数量
size_t triggerPendingWindows();
```

---

### 5.6 水位线管理

```cpp
void    updateWatermark(int64_t watermark_us);  // 主动设置水位线（单位：μs）
int64_t getWatermark() const;                   // 读取当前水位线
```

---

### 5.7 回调注册

```cpp
using WindowCallback = std::function<void(const WindowInfo&, const ComputeStatus&)>;

void onWindowCompleted(WindowCallback callback);  // 窗口计算成功后触发
void onWindowFailed(WindowCallback callback);     // 窗口计算失败后触发
```

回调在 `ResourceHandle::submitTask()` 提交的线程中执行，**不在调度线程中**。回调函数内不应抛出异常（内部 catch 后仅打印，不会传播）。

---

### 5.8 监控

```cpp
SchedulingMetrics       getMetrics() const;
std::vector<WindowInfo> getAllWindows() const;
WindowInfo              getWindowInfo(uint64_t window_id) const;
size_t                  getPendingWindowCount() const;
size_t                  getActiveWindowCount() const;
void                    reset();  // 清除所有窗口状态
```

**`SchedulingMetrics` 字段：**

```cpp
struct SchedulingMetrics {
    uint64_t total_windows_scheduled  = 0;
    uint64_t total_windows_completed  = 0;
    uint64_t total_windows_failed     = 0;
    uint64_t pending_windows          = 0;
    uint64_t active_windows           = 0;

    double   avg_scheduling_latency_ms  = 0.0;
    double   avg_window_completion_ms   = 0.0;
    double   max_window_completion_ms   = 0.0;

    double   windows_per_second         = 0.0;
    double   tuples_per_second          = 0.0;

    uint64_t late_data_count            = 0;
    uint64_t late_windows_recomputed    = 0;
};
```

---

## 6. `ComputeStateManager` API（可选）

**文件**：`include/sage_tsdb/compute/compute_state_manager.h`  
**用途**：持久化 PECJ 算子的运行状态（水位线、窗口进度、算子内部状态），支持故障恢复。

```cpp
// 构造：需要 TimeSeriesDB 指针来存储状态
explicit ComputeStateManager(TimeSeriesDB* db);

// 保存/加载
bool saveState(const std::string& compute_name, const ComputeState& state);
bool loadState(const std::string& compute_name, ComputeState& state);
bool hasState(const std::string& compute_name) const;
bool deleteState(const std::string& compute_name);
std::vector<std::string> listStates() const;

// 持久化到磁盘（触发 LSM-Tree flush）
// compute_name 为空字符串时持久化全部引擎状态
bool persistState(const std::string& compute_name = "");

// Checkpoint（不可变快照）
bool createCheckpoint(const std::string& compute_name, uint64_t checkpoint_id);
bool restoreCheckpoint(const std::string& compute_name,
                       uint64_t checkpoint_id,
                       ComputeState& state);
std::vector<std::pair<uint64_t, std::map<std::string, int64_t>>>
     listCheckpoints(const std::string& compute_name) const;
bool deleteCheckpoint(const std::string& compute_name, uint64_t checkpoint_id);

// 序列化工具（静态方法，可独立使用）
static std::vector<uint8_t> serialize(const ComputeState& state);
static bool deserialize(const std::vector<uint8_t>& data, ComputeState& state);
```

**`ComputeState` 关键字段：**

```cpp
struct ComputeState {
    std::string              compute_name;     // 计算引擎标识符
    int64_t                  timestamp;        // 快照时间戳
    int64_t                  watermark;        // 当前水位线
    uint64_t                 window_id;        // 当前处理到的窗口 ID
    uint64_t                 processed_events; // 已处理事件总数
    std::vector<uint8_t>     operator_state;   // 算子内部状态（不透明字节）
    std::map<std::string, std::string> metadata;
};
```

---

## 7. 完整调用流程

### 7.1 初始化顺序

```cpp
// 1. 创建线程池适配器（调用方实现）
auto resource_handle = std::make_unique<EnterpriseResourceHandle>(thread_pool);

// 2. 创建数据库（指向企业DB后端）
sage_tsdb::core::StorageBackendConfig backend_cfg;
backend_cfg.backend = "dameng";
backend_cfg.params  = {{"host","127.0.0.1"}, {"port","5236"}, {"user","SYSDBA"}};
auto db = std::make_unique<sage_tsdb::TimeSeriesDB>(backend_cfg);

// 3. 建表
db->createTable("stream_s", sage_tsdb::TableType::Stream);
db->createTable("stream_r", sage_tsdb::TableType::Stream);

// 4. 配置并初始化计算引擎
sage_tsdb::compute::ComputeConfig compute_cfg;
compute_cfg.operator_type = "IMA";
compute_cfg.window_len_us = 2'000'000;  // 2s
compute_cfg.slide_len_us  = 1'000'000;  // 1s

auto engine = std::make_unique<sage_tsdb::compute::PECJComputeEngine>();
engine->initialize(compute_cfg, db.get(), nullptr);
// resource_handle 传 nullptr 即可，引擎本身不使用它

// 5. 配置并创建调度器
sage_tsdb::compute::WindowSchedulerConfig sched_cfg;
sched_cfg.window_len_us  = compute_cfg.window_len_us;
sched_cfg.slide_len_us   = compute_cfg.slide_len_us;
sched_cfg.trigger_policy = sage_tsdb::compute::TriggerPolicy::Hybrid;

// TableManager 当前未被使用，传入空实例即可
auto table_mgr = std::make_unique<sage_tsdb::TableManager>();

auto scheduler = std::make_unique<sage_tsdb::compute::WindowScheduler>(
    sched_cfg,
    engine.get(),
    table_mgr.get(),
    resource_handle.get()   // ← WindowScheduler 才实际使用 resource_handle
);

// 6. 注册回调
scheduler->onWindowCompleted([](const auto& win, const auto& status) {
    printf("Window %lu done: join_count=%zu, time=%.1fms\n",
           win.window_id, status.join_count, status.computation_time_ms);
});
scheduler->onWindowFailed([](const auto& win, const auto& status) {
    fprintf(stderr, "Window %lu failed: %s\n",
            win.window_id, status.error.c_str());
});

// 7. 启动调度器
scheduler->watchTable("stream_s", 0);
scheduler->watchTable("stream_r", 1);
scheduler->start();
```

### 7.2 数据写入（运行期）

```cpp
void write_event(sage_tsdb::TimeSeriesDB& db,
                 sage_tsdb::compute::WindowScheduler& scheduler,
                 const std::string& stream,
                 int64_t event_time_us,
                 uint64_t key,
                 double payload)
{
    sage_tsdb::TimeSeriesData record;
    record.timestamp       = event_time_us;
    record.value           = payload;
    record.tags["key"]     = std::to_string(key);
    record.fields["value"] = std::to_string(payload);

    db.insert(stream, record);

    // ★ 写入后必须通知调度器
    scheduler.onDataInserted(stream, event_time_us);
}
```

### 7.3 优雅关闭

```cpp
scheduler->stop(true);   // 等待所有活跃窗口计算完成
// engine / db / resource_handle 按创建逆序析构
```

---

## 8. 编译宏说明

| 宏 | 含义 |
|---|---|
| `PECJ_MODE_INTEGRATED` | 启用计算引擎和调度器的完整代码；未定义时头文件中所有内容被 `#ifdef` 隐藏 |
| `PECJ_FULL_INTEGRATION` | 实际链接 PECJ 算子库；未定义时引擎以 Stub 模式运行（返回成功但 `join_count=0`） |

**CMake 配置：**

```cmake
target_compile_definitions(your_target PRIVATE
    PECJ_MODE_INTEGRATED
    PECJ_FULL_INTEGRATION      # 需要 PECJ 库已编译
)
target_link_libraries(your_target PRIVATE
    sage_tsdb_pecj_engine
    IntelliStreamOoOJoin       # PECJ 动态库
)
target_include_directories(your_target PRIVATE
    ${PECJ_DIR}/include        # PECJ 头文件路径
)
```

---

## 9. 已知注意事项

### ① 两个 `TimeRange` 的边界语义不同

两个 `TimeRange` 字段名已统一为 `start_us` / `end_us`，单位均为微秒，但边界语义有所不同：

| | `sage_tsdb::compute::TimeRange` | `sage_tsdb::TimeRange` |
|---|---|---|
| 所在文件 | `compute/pecj_compute_engine.h` | `core/time_series_data.h` |
| 起始字段 | `start_us`（微秒） | `start_us`（微秒） |
| 结束字段 | `end_us`（微秒） | `end_us`（微秒） |
| 边界语义 | 左闭右开 `[start, end)` | 左闭右闭 `[start, end]` |

调用 `executeWindowJoin()` 时传入 `compute::TimeRange`，引擎直接将其传给 `db->query()`，无需手动转换。

### ② `table_manager_` 构造函数要求非空但从未调用

`WindowScheduler` 构造函数会对 `table_manager` 做非空断言（`src/compute/window_scheduler.cpp` 第 42 行），但实现中没有任何实际调用。传入 `new TableManager()` 空实例即可绕过此检查，无需为其提供实际的存储实现。

### ③ 结果写回功能未实现

`writeResults()` 当前是空实现（`src/compute/pecj_compute_engine.cpp` 第 550–576 行为全注释的 TODO）。连接结果**不会**被自动写入 `result_table`，只通过 `ComputeStatus.join_count` 和 `onWindowCompleted` 回调返回。如需持久化结果，在回调中自行写入企业DB。

### ④ MSWJ 算子必须通过工厂函数创建

引擎内部已处理此问题，对外透明。若调用方需要在引擎之外直接构造 `OoOJoin::MSWJOperator`，必须使用：

```cpp
// 文件：include/sage_tsdb/detail/pecj_mswj_factory.h
auto op = sage_tsdb::detail::createMSWJOperator(cfg);
```

直接默认构造后调用 `setConfig()` 会因内部 `streamOperator` 为空指针而 crash。
