# sageTSDB × 企业级数据库（达梦 DM）集成 — SDK 库开发执行计划

版本: v0.2（草案）
日期: 2026-07-06
适用范围: `sageTSDB` 当前代码库
课题目标: 完成 SDK（库）功能开发，为把 **PECJ** 与 **SRTFD** 接入企业级数据库（达梦 DM）做数据库集成与适配
关联文档: [DESIGN_DOC_SAGETSDB_PECJ.md](DESIGN_DOC_SAGETSDB_PECJ.md)、[adr/0001-boundary-and-mode-policy.md](adr/0001-boundary-and-mode-policy.md)

---

## 1. 分工与交付边界（本计划的前提）

本课题的数据库集成分为两侧，本计划**只负责 SDK 库一侧**：

| | 负责方 | 职责 |
| --- | --- | --- |
| **SDK 库侧（本计划）** | 本方 | 存储后端抽象接口、Mock/内存后端、**达梦适配器骨架（TODO 占位）**、文档、环境/构建配置。语言以 **C++ core** 为主。 |
| **达梦实例侧** | 课题另一方 | 真实达梦部署、schema、数据、驱动连接；把连接/SQL 代码填入本方交付的适配器骨架。 |

**对接点（Handoff）= 本方交付的 `IStorageBackend` 契约 + 适配器骨架里标注好的填充位（TODO）。** 双方通过这个接口对接，互不阻塞：

- 本方不需要真实达梦实例即可完成开发、编译和测试（用 Mock/内存后端）。
- 另一方拿到骨架和契约文档后，只需实现接口方法，无需理解 PECJ/SRTFD 或 sageTSDB 内部。

> **本计划不包含**：真实达梦连接联调、schema 落地、数据导入、端到端跑通达梦。这些属于达梦实例侧，或在双方联调阶段进行（见 §7）。

### 关键结论（决定整个设计）

> **PECJ 和 SRTFD 的计算逻辑不需要改。** 它们已是 database-centric：只通过 `TimeSeriesDB` 的 `query()` / `insert()` / `createTable()` 读写数据，不持有外部数据源。
> 因此 SDK 侧的工作是：**给 `TimeSeriesDB` 抽出一个可插拔存储后端接口 `IStorageBackend`，默认走内存实现（现状不变），并预留达梦适配器骨架供另一方填充。**

这把课题从"改两个算法"收敛为"加一层存储抽象 + 定义清晰的对接契约 + 交付可编译的骨架"。

---

## 2. 现状分析（基于当前代码）

### 2.1 数据只从一个入口进出

两个计算引擎对数据的全部访问，都集中在对 `TimeSeriesDB` 指针的少数几次调用上：

| 引擎 | 文件 | 读 | 写 | 建表/判存 |
| --- | --- | --- | --- | --- |
| SRTFD | [src/compute/srtfd_compute_engine.cpp](../src/compute/srtfd_compute_engine.cpp) | `db_->query(input_table, time_range)` (L143) | `db_->insert(result_table, result)` (L300) | `db_->hasTable` (L93/96)、`db_->createTable` (L97) |
| PECJ | [src/compute/pecj_compute_engine.cpp](../src/compute/pecj_compute_engine.cpp) | `db_->query(stream_s_table, range)`、`db_->query(stream_r_table, range)` (L275-276) | 结果写回（L553 附近，本就需收口） | 依赖表已建 |

引擎初始化签名统一：`initialize(config, TimeSeriesDB* db, core::ResourceHandle* resource_handle)`。**只要 `TimeSeriesDB` 的读写能切换到其他后端，引擎代码零改动。**

### 2.2 当前 TimeSeriesDB 的存储是硬编码的内存实现

[include/sage_tsdb/core/time_series_db.h](../include/sage_tsdb/core/time_series_db.h) 里：

- `std::unique_ptr<TimeSeriesIndex> index_;`（默认表）
- `std::unordered_map<std::string, std::unique_ptr<TimeSeriesIndex>> tables_;`（命名多表）
- `std::unique_ptr<StorageEngine> storage_engine_;`（本地 LSM 文件持久化）

`TimeSeriesIndex`（[include/sage_tsdb/core/time_series_index.h](../include/sage_tsdb/core/time_series_index.h)）是纯内存的 `std::vector<TimeSeriesData> data_` + tag 倒排索引。**当前没有任何存储后端抽象接口**——这是 SDK 侧要补的第一块，也是对接达梦的插槽。

### 2.3 相关真实类型（契约以此为准）

以下签名摘自当前头文件，接口契约必须与之一致：

```cpp
// include/sage_tsdb/core/time_series_data.h
using TimeSeriesValue = std::variant<double, std::vector<double>>;  // 标量或向量
using Tags   = std::map<std::string, std::string>;                 // 可索引
using Fields = std::map<std::string, std::string>;                 // 元数据

struct TimeSeriesData {
    int64_t timestamp;      // 【契约固定：微秒 μs】结构体注释历史写作毫秒，本契约统一按微秒解释与存取
    TimeSeriesValue value;  // double 或 vector<double>（SRTFD 默认 52 维）
    Tags tags;
    Fields fields;
};

struct TimeRange { int64_t start_time; int64_t end_time; };   // 两端 inclusive
struct QueryConfig {
    TimeRange time_range;
    Tags filter_tags;
    AggregationType aggregation = AggregationType::NONE;
    int64_t window_size = 0;
    int32_t limit = 1000;
};
```

`TableType`（[time_series_db.h](../include/sage_tsdb/core/time_series_db.h)）：`TimeSeries / Stream / JoinResult / ComputeState`。

**时间语义（契约固定）**：所有经过 `IStorageBackend` 的 `timestamp`、`TimeRange.start_time`/`end_time`、`QueryConfig.window_size` 一律以**微秒（μs）** 为单位。`TimeRange` 两端 **inclusive**。达梦侧 `timestamp` 列存微秒整数（`BIGINT`），不做隐式单位换算；任何毫秒来源数据必须在进入后端前转换。

### 2.4 达梦驱动尚不存在

全仓库检索 `dameng / DM8 / dmPython / dpi.h` 只命中路径字符串，**没有任何达梦连接代码**。本方交付骨架，连接代码由另一方填充。

---

## 3. 核心设计：可插拔存储后端（SDK 契约）

### 3.1 目标架构

```mermaid
flowchart TB
    subgraph Compute[计算层 — 不改动]
        PECJ[PECJComputeEngine]
        SRTFD[SRTFDComputeEngine]
    end
    DB[TimeSeriesDB<br/>统一读写 API]
    subgraph Backend[IStorageBackend 抽象 — 本方交付]
        Mem[MemoryBackend<br/>现有 TimeSeriesIndex 封装]
        DM[DamengBackend 骨架<br/>本方给结构+TODO / 另一方填连接]
    end
    DMDB[(达梦 DM<br/>另一方负责)]

    PECJ -->|query/insert| DB
    SRTFD -->|query/insert| DB
    DB --> Mem
    DB --> DM
    DM -.填充后.-> DMDB
```

### 3.2 抽象接口 `IStorageBackend`（本方交付①）

新增 `include/sage_tsdb/core/storage_backend.h`，覆盖 `TimeSeriesDB` 当前对索引的全部依赖：

```cpp
namespace sage_tsdb::core {

// 后端只负责“表 × 时序记录”的读写，不承担计算、调度、资源管理
class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    // 表管理
    virtual bool createTable(const std::string& name, TableType type) = 0;
    virtual bool dropTable(const std::string& name) = 0;
    virtual bool hasTable(const std::string& name) const = 0;
    virtual std::vector<std::string> listTables() const = 0;

    // 写入（返回值语义与现 TimeSeriesDB::insert 一致）
    virtual size_t insert(const std::string& table, const TimeSeriesData& d) = 0;
    virtual std::vector<size_t> insertBatch(const std::string& table,
                                            const std::vector<TimeSeriesData>& d) = 0;

    // 查询（时间范围 + tag 过滤 + limit，语义对齐 QueryConfig）
    virtual std::vector<TimeSeriesData> query(const std::string& table,
                                              const QueryConfig& cfg) const = 0;

    // 统计 / 生命周期
    virtual size_t size(const std::string& table) const = 0;
    virtual void   clear(const std::string& table) = 0;
    virtual bool   flush() = 0;   // 提交/落盘；内存后端可为 no-op

    // 后端标识（便于日志与显式模式判断）
    virtual std::string backendName() const = 0;
};

} // namespace sage_tsdb::core
```

配套一个后端工厂 / 配置结构（`StorageBackendConfig`：backend 名、连接参数 key-value），供 `TimeSeriesDB` 按显式配置选择后端。

### 3.3 MemoryBackend（本方交付②，兼回归基线）

把现有 `TimeSeriesIndex` 逻辑封装为 `IStorageBackend` 实现，作为默认后端。目标是**"不启用达梦时，行为与当前完全一致"**——现有所有测试不改一行即通过。这既是默认实现，也是达梦后端的差分测试对照组。

### 3.4 DamengBackend 骨架（本方交付③，TODO 占位）

新增 `include/sage_tsdb/core/backends/dameng_backend.h` 与 `src/core/backends/dameng_backend.cpp`：

- 完整实现 `IStorageBackend` 的**类结构、方法签名、参数校验、错误路径、日志点**。
- 真正的连接/SQL/绑定处，用清晰的 `// TODO(DM):` 占位并配注释说明期望行为、输入输出、约束。
- 在未填充（或未启用 `SAGE_TSDB_ENABLE_DM`）时，方法应 **fail-fast 返回明确错误**（遵循 ADR 0001，禁止静默 fallback 到内存）。
- 交付时**可编译**（骨架不依赖真实达梦库即可 build；真实驱动作为可选依赖）。

TODO 占位示例（骨架里标注给另一方的填充位）：

```cpp
size_t DamengBackend::insert(const std::string& table, const TimeSeriesData& d) {
    // 契约：把一条记录写入达梦表 ts_<table>，返回该表当前行序号语义的 id
    // 输入：d.timestamp(int64 ms)、d.value(标量或向量)、d.tags、d.fields
    // 约束：参数化绑定，禁止字符串拼接 SQL；批量走 insertBatch
    // TODO(DM): 用达梦驱动（DPI/ODBC）实现参数化插入
    throw NotImplemented("DamengBackend::insert — DM driver not wired");
}
```

### 3.5 表结构映射建议（写进契约文档，供另一方参考）

`TimeSeriesData` 字段 → 达梦表设计建议（**最终 DDL 由另一方决定**，本方只给映射契约）：

| sageTSDB 概念 | 达梦表设计建议 | 说明 |
| --- | --- | --- |
| 命名表（如 `stream_s`） | 物理表 `ts_stream_s` | 前缀避免冲突 |
| `timestamp` | `BIGINT` + 索引 | 范围查询主路径 |
| 标量 `value` | `DOUBLE` | 标量场景 |
| 向量 `value`（SRTFD 52 维） | `VARBINARY`/`BLOB` 序列化 **或** 宽表 `f0..f51` | 见 §3.6 |
| `tags` | 高频过滤 tag 拆列 + 索引，或 `CLOB(JSON)` | 过滤下推优先 |
| `fields` | `CLOB(JSON)` | 非索引元数据 |

### 3.6 向量值序列化（SRTFD 关键点，本方给定字节格式）

SRTFD 默认 `input_dim = 52`，每条样本是 52 维向量。`value` 是 `std::variant<double, std::vector<double>>`，两种形态都用**同一套字节格式**编码进达梦的 `VARBINARY`/`BLOB` 列（方案 A）。本方给定格式，Mock 与达梦共用同一编解码器，配单元测试保证往返一致。

**字节格式 `stsb1`（sageTSDB blob v1），全部小端（little-endian）：**

| 偏移 | 大小(字节) | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `"STSB"`（0x53 0x54 0x53 0x42） |
| 4 | 1 | version | `0x01` |
| 5 | 1 | kind | `0x01`=标量 double；`0x02`=向量 double[] |
| 6 | 2 | reserved | 置 0，对齐用 |
| 8 | 4 | count | 元素个数（`uint32`）：标量恒为 1，向量为维度数（如 52） |
| 12 | 8×count | data | `count` 个 IEEE-754 `double`（每个 8 字节，小端） |

- 总长度 = `12 + 8 × count` 字节。标量为 20 字节，52 维向量为 428 字节。
- 解码时校验 magic、version、`kind` 与 `count` 的一致性（标量必须 `count==1`）；不匹配 fail-fast。
- 契约文档（D5）会附上该格式的编码/解码参考实现和测试向量（含一条标量、一条 52 维样例的十六进制）。

方案 B（宽表 `f0..f51`）作为可选优化留在文档，不进第一版骨架。

---

## 4. 交付物清单（对应本课题 SDK 库开发）

| # | 交付物 | 内容 | 形式 |
| --- | --- | --- | --- |
| D1 | **抽象接口（头文件/契约）** | `IStorageBackend` + `StorageBackendConfig` + 后端工厂；**每个公共方法配 Doxygen 注释** | `include/sage_tsdb/core/storage_backend.h` |
| D2 | **Mock/内存后端实现** | `MemoryBackend`（封装 `TimeSeriesIndex`），默认后端 & 回归基线；公共方法配 Doxygen 注释 | `include/sage_tsdb/core/backends/memory_backend.h` + `src/core/backends/memory_backend.cpp` |
| D3 | **达梦适配器骨架（TODO 占位）** | `DamengBackend` 完整结构 + 填充点 + fail-fast；**每个方法 + 每个 `// TODO(DM):` 配 Doxygen/契约注释** | `include/sage_tsdb/core/backends/dameng_backend.h` + `src/core/backends/dameng_backend.cpp` |
| D4 | **TimeSeriesDB 改造** | 内部改为持有 `IStorageBackend`，按配置注入后端 | 改 `time_series_db.{h,cpp}` |
| D5 | **文档** | 接口契约说明、达梦适配对接指南（另一方如何填 TODO）、表映射与序列化约定、配置说明 | `docs/STORAGE_BACKEND_CONTRACT.md`（新增）+ 更新设计文档 |
| D6 | **环境/构建配置** | `SAGE_TSDB_ENABLE_DM` 开关、达梦库探测模板、连接配置模板 | 改 `CMakeLists.txt` + 配置样例 |
| D7 | **测试** | Mock 后端回归（旧测试全绿）+ 针对接口契约的后端一致性测试脚手架 | `tests/` |

---

## 5. 分阶段执行计划（SDK 侧，均不需真实达梦）

### 阶段 1：存储后端抽象 + 内存后端（D1、D2、D4）

- [ ] 新增 `IStorageBackend`（§3.2）与 `StorageBackendConfig`、后端工厂。
- [ ] 实现 `MemoryBackend`：封装现有 `TimeSeriesIndex`，逐字段对齐当前语义。
- [ ] 改造 `TimeSeriesDB`：内部持有 `std::unique_ptr<IStorageBackend>`，默认注入 `MemoryBackend`；`query/insert/createTable/...` 转发到后端。
- [ ] **验收门禁**：现有全部测试（`test_time_series_db`、`test_srtfd_compute_engine` 等）**不改一行、全绿**。这是零行为变化的证明。

### 阶段 2：达梦适配器骨架 + 构建开关（D3、D6）

- [ ] CMake 新增 `option(SAGE_TSDB_ENABLE_DM OFF)` + 达梦库 `find_library`/`find_path` 模板（缺失时给清晰提示，不静默跳过）。
- [ ] 编写 `DamengBackend` 骨架：完整方法签名、参数校验、错误路径、日志点、`// TODO(DM):` 填充位与注释契约（§3.4）。
- [ ] 未启用/未填充时 fail-fast，明确报错，**绝不回退内存**（ADR 0001）。
- [ ] **验收**：`-DSAGE_TSDB_ENABLE_DM=OFF` 默认构建不受影响；`-DSAGE_TSDB_ENABLE_DM=ON` 但无真实驱动时，骨架仍可编译，运行时对未实现方法明确报错。

### 阶段 3：契约文档与对接指南（D5）

- [ ] `docs/STORAGE_BACKEND_CONTRACT.md`：逐方法说明输入/输出/线程安全/错误语义/`limit`/tag 过滤/时间范围边界（inclusive）等。
- [ ] 达梦对接指南：另一方"如何填 TODO"、表映射建议（§3.5）、向量序列化约定（§3.6）、连接配置字段清单、凭据从环境变量读取的约定。
- [ ] 更新 [DESIGN_DOC_SAGETSDB_PECJ.md](DESIGN_DOC_SAGETSDB_PECJ.md) 增补"存储后端抽象与达梦适配"章节（设计文档维护规则要求同步）。

### 阶段 4：后端一致性测试脚手架（D7）

- [ ] 写一组"后端无关"的读写用例（建表→批量插入→范围查询→tag 过滤→limit→向量往返）。
- [ ] 对 `MemoryBackend` 全绿；同一组用例对 `DamengBackend` 预置为跳过/待另一方填充后启用（差分测试框架就位）。
- [ ] **验收**：Mock 侧一致性测试通过；达梦侧测试框架就绪、标注为 pending。

### 阶段 5（可选，双方联调）：达梦真实链路

- 由另一方填充 `DamengBackend` 的 TODO 后，双方联调：内存后端 vs 达梦后端差分为 0，再跑 SRTFD/PECJ 端到端。**此阶段不在本方 SDK 交付范围内**，列出仅为衔接。

---

## 6. 里程碑与工作量估算（SDK 侧）

| 里程碑 | 交付物 | 估算 |
| --- | --- | --- |
| M1 抽象层 + 内存后端落地 | D1、D2、D4，旧测试全绿 | ~2 天 |
| M2 达梦骨架 + 构建开关 | D3、D6，两种开关均可编译 | ~2 天 |
| M3 契约文档与对接指南 | D5 | ~1.5 天 |
| M4 一致性测试脚手架 | D7 | ~1 天 |

合计约 **6–7 个工作日**（纯 SDK 侧，不含另一方达梦实现与联调）。

---

## 7. 风险与对策

| 风险 | 影响 | 对策 |
| --- | --- | --- |
| 接口契约与另一方达梦能力不匹配（如某查询无法下推） | 联调返工 | 契约文档明确"必须实现 vs 可选下推"；查询语义以 `MemoryBackend` 为准绳 |
| timestamp 单位不一致（data.h 注释毫秒，PECJ adapter 用微秒） | 时间范围查询错位 | **已定：契约统一微秒（§时间语义）。** 毫秒来源数据在进入后端前转换；达梦列存微秒 BIGINT |
| 向量序列化格式双方理解不一致 | 52 维数据往返出错 | **已定：本方给定字节格式 `stsb1`（§3.6）。** Mock 与达梦共用同一编解码器 + 测试向量 |
| 引入静默 fallback（达梦失败回退内存）掩盖问题 | 违反 ADR 0001 | 后端显式配置；失败 fail-fast；骨架未实现方法明确报错 |
| `_GLIBCXX_USE_CXX11_ABI=0` 与达梦库 ABI 冲突 | 另一方链接失败 | 构建模板注明 ABI 约束，作为对接指南的已知事项交给另一方 |
| 达梦凭据泄露 | 安全 | 密码走环境变量/配置文件；日志与代码不出现明文；表引用按 key 名不打印值 |

---

## 8. 决策记录与待确认事项

### 已决策（本次确认，写入契约）

- **D-1 时间戳单位 = 微秒（μs）**：所有经 `IStorageBackend` 的时间字段统一微秒，`TimeRange` 两端 inclusive，达梦列存微秒 `BIGINT`。详见 §2.3 时间语义。
- **D-2 接口方法集 = 最小可用集**：第一版即 §3.2 的 `createTable / dropTable / hasTable / listTables / insert / insertBatch / query / size / clear / flush / backendName`。事务边界、truncate、按 window 删除旧结果等留作后续扩展，不进第一版。
- **D-3 向量序列化 = 本方给定字节格式 `stsb1`**：格式见 §3.6，Mock 与达梦共用同一编解码器并附测试向量。

- **D-4 命名与目录**：无既定命名规范约束，采用本文建议 —— 接口 `IStorageBackend`（`sage_tsdb::core` 命名空间），头文件 `include/sage_tsdb/core/storage_backend.h`；后端实现放 `include/sage_tsdb/core/backends/` 与 `src/core/backends/`（`memory_backend.*`、`dameng_backend.*`）。
- **D-5 代码文档 = Doxygen 注释**：`IStorageBackend`、`MemoryBackend`、`DamengBackend` 骨架的**每个公共方法**都配 Doxygen 注释（`@brief`/`@param`/`@return`/`@note`），并对每个 `// TODO(DM):` 填充位写明期望行为、输入输出、约束与错误语义，方便另一方直接照注释填连接代码。契约文档（D5 交付）与代码注释保持一致。

### 待确认

（暂无——接口契约层面的关键决策已齐备，可据此落头文件。）

---

## 附录 A：关键代码坐标

- 计算引擎读写唯一入口（不改动）：
  - SRTFD: [src/compute/srtfd_compute_engine.cpp:143](../src/compute/srtfd_compute_engine.cpp#L143)、[:300](../src/compute/srtfd_compute_engine.cpp#L300)
  - PECJ: [src/compute/pecj_compute_engine.cpp:275-276](../src/compute/pecj_compute_engine.cpp#L275)
- 需增加后端抽象的类：[include/sage_tsdb/core/time_series_db.h](../include/sage_tsdb/core/time_series_db.h)（`index_` / `tables_` / `storage_engine_`）
- 现内存实现（将封装为 MemoryBackend）：[include/sage_tsdb/core/time_series_index.h](../include/sage_tsdb/core/time_series_index.h)
- 契约依赖的真实类型：[include/sage_tsdb/core/time_series_data.h](../include/sage_tsdb/core/time_series_data.h)
- 构建开关参照：[CMakeLists.txt:61-66](../CMakeLists.txt#L61)
- 模式边界原则（禁止静默 fallback）：[docs/adr/0001-boundary-and-mode-policy.md](adr/0001-boundary-and-mode-policy.md)
