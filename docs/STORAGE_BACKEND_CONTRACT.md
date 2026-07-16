# sageTSDB 存储后端契约与达梦对接指南

版本: v1.0
日期: 2026-07-06
适用范围: `sageTSDB` 存储后端抽象（`IStorageBackend`）及达梦（DM）适配器骨架
关联文档: [EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md](EXECUTION_PLAN_ENTERPRISE_DB_INTEGRATION.md)、[adr/0001-boundary-and-mode-policy.md](adr/0001-boundary-and-mode-policy.md)

---

## 1. 本文是什么

本文是 **SDK 库侧交付给达梦实例侧的对接契约**（交付物 D5）。它规定：

- `IStorageBackend` 每个方法的**输入、输出、错误语义、线程安全**要求。
- 时间单位、区间边界、值序列化等**必须一致的约定**。
- 达梦适配器骨架里每个 `// TODO(DM):` 的**期望行为**与填充方法。
- 正确性判定基准：**达梦后端对同一输入的结果必须与 `MemoryBackend` 一致**。

另一方无需理解 PECJ/SRTFD 或 sageTSDB 内部，只需按本契约实现 `IStorageBackend` 的方法即可。

对接点（Handoff）：
- 接口：[include/sage_tsdb/core/storage_backend.h](../include/sage_tsdb/core/storage_backend.h)
- 达梦骨架：[include/sage_tsdb/core/backends/dameng_backend.h](../include/sage_tsdb/core/backends/dameng_backend.h)、[src/core/backends/dameng_backend.cpp](../src/core/backends/dameng_backend.cpp)
- 值编解码：[include/sage_tsdb/core/value_codec.h](../include/sage_tsdb/core/value_codec.h)
- 一致性测试：[tests/test_storage_backend_contract.cpp](../tests/test_storage_backend_contract.cpp)

---

## 2. 数据模型（契约依赖的真实类型）

摘自 [include/sage_tsdb/core/time_series_data.h](../include/sage_tsdb/core/time_series_data.h)，实现必须与之一致：

```cpp
using TimeSeriesValue = std::variant<double, std::vector<double>>; // 标量或向量
using Tags   = std::map<std::string, std::string>;                 // 可索引
using Fields = std::map<std::string, std::string>;                 // 元数据

struct TimeSeriesData {
    int64_t timestamp;      // 【契约固定：微秒 μs】
    TimeSeriesValue value;  // double 或 vector<double>（SRTFD 默认 52 维）
    Tags tags;
    Fields fields;
};

struct TimeRange { int64_t start_time; int64_t end_time; };  // 两端 inclusive
struct QueryConfig {
    TimeRange time_range;
    Tags filter_tags;
    AggregationType aggregation = AggregationType::NONE;
    int64_t window_size = 0;
    int32_t limit = 1000;   // <=0 表示不限制
};
```

`TableType`：`TimeSeries / Stream / JoinResult / ComputeState`。

### 时间语义（必须遵守）

- 所有 `timestamp`、`TimeRange.start_time/end_time`、`QueryConfig.window_size` 单位为**微秒（μs）**。
- `TimeRange` 两端**闭区间（inclusive）**：`start_time <= ts <= end_time`。
- 达梦 `timestamp` 列存微秒整数（`BIGINT`），**不做隐式单位换算**。

---

## 3. IStorageBackend 方法契约

后端只负责"表 × 时序记录"的读写，不承担计算、调度、资源管理。**错误处理总则**：不可恢复错误一律抛异常，绝不静默回退到其他后端（ADR 0001）：

- `core::NotImplemented`（`std::logic_error` 子类）：方法尚未接线（骨架状态）。
- `std::runtime_error`：真实的数据库/驱动错误（表不存在、连接失败、SQL 失败等）。

> 两者必须区分开：调用方与测试据此判断"还没填" vs "数据库真的报错"。

| 方法 | 输入 | 返回/输出 | 错误语义 |
| --- | --- | --- | --- |
| `createTable(name, type)` | 逻辑表名、表类型 | 新建返回 `true`；已存在返回 `false`（**非错误**） | 空表名等非法输入抛 `runtime_error` |
| `dropTable(name)` | 逻辑表名 | 存在并删除返回 `true`；否则 `false` | 同上 |
| `hasTable(name)` const | 逻辑表名 | 存在返回 `true` | 非法输入返回 `false` |
| `listTables()` const | — | 全部逻辑表名（顺序不限） | — |
| `insert(table, data)` | 表名、单条记录 | 该记录的行序号（语义同历史 `TimeSeriesDB::insert`，如插入前行数） | 表不存在/写失败抛 `runtime_error` |
| `insertBatch(table, list)` | 表名、批量记录 | 与 `list` 对齐的行序号数组 | 同上；**性能关键路径，须批量绑定** |
| `query(table, config)` const | 表名、查询配置 | 匹配记录；顺序/聚合语义**以 `MemoryBackend` 为准** | 表不存在/查询失败抛 `runtime_error` |
| `size(table)` const | 表名 | 记录数；表不存在返回 `0` | — |
| `clear(table)` | 表名 | 清空该表所有行、保留表；表不存在为 no-op | — |
| `flush()` | — | 成功返回 `true`；内存后端可为 no-op | — |
| `backendName()` const | — | 后端名，如 `"memory"`、`"dameng"` | — |

### 查询语义细则（`MemoryBackend` 基准）

- **时间范围**：闭区间 `[start_time, end_time]`。
- **tag 过滤**：`filter_tags` 中每个键值对都必须匹配（AND 语义）；任一 tag 键或值不存在则结果为空。
- **limit**：`limit > 0` 时最多返回 `limit` 条；`limit <= 0` 表示不限制。
- **顺序**：按 timestamp 升序（`MemoryBackend` 底层对乱序数据排序后返回）。达梦侧应以 `ORDER BY ts` 保证一致。

线程安全：方法应可被多线程并发调用；实现须在文档中说明其具体保证（`MemoryBackend` 以每表内部读写锁保证并发读写安全）。

---

## 4. 值序列化：`stsb1` 字节格式

`value` 是标量或向量。当后端以不透明 blob 存储值（达梦的 `VARBINARY`/`BLOB` 列，方案 A）时，**必须**使用 `stsb1` 格式，保证与 `MemoryBackend` 往返一致。参考实现见 [include/sage_tsdb/core/value_codec.h](../include/sage_tsdb/core/value_codec.h)（`sage_tsdb::core::stsb1::encode/decode`），达梦侧可直接复用该头，或按下表自行实现并用第 6 节测试向量对拍。

字节布局（全部小端 little-endian）：

| 偏移 | 大小(字节) | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | ASCII `"STSB"`（`0x53 0x54 0x53 0x42`） |
| 4 | 1 | version | `0x01` |
| 5 | 1 | kind | `0x01`=标量 double；`0x02`=向量 double[] |
| 6 | 2 | reserved | 置 0，对齐用 |
| 8 | 4 | count | 元素个数（`uint32`）：标量恒为 1，向量为维度 |
| 12 | 8×count | data | `count` 个 IEEE-754 `double`（各 8 字节，小端） |

- 总长 = `12 + 8×count`。标量 20 字节，52 维向量 428 字节。
- 解码须校验 magic、version、kind 与 count 一致性（标量必须 `count==1`），不匹配抛 `runtime_error`。

**固定测试向量（标量 `1.0`，用于对拍编码器）**：

```
53 54 53 42 01 01 00 00 01 00 00 00 00 00 00 00 00 00 F0 3F
└── STSB ──┘ v  k  ─rsv─ └─ count=1 ─┘ └──── 1.0 (LE) ────┘
```

该向量在 [tests/test_storage_backend_contract.cpp](../tests/test_storage_backend_contract.cpp) 的 `Stsb1Codec.FixedHeaderBytes` 中逐字节断言。

---

## 5. 达梦对接指南：如何填充骨架

骨架已给出完整类结构、参数校验、错误路径、日志点，真正的驱动调用处标有 `// TODO(DM):`。填充步骤：

### 5.1 表结构映射建议

逻辑表名 `<name>` → 物理表 `<table_prefix><name>`（默认前缀 `ts_`，见 `physicalTable()`）。列建议：

| sageTSDB 概念 | 达梦列 | 说明 |
| --- | --- | --- |
| `timestamp` | `BIGINT` + 索引 | 微秒；范围查询主路径，建议按时间分区 |
| `value` | `VARBINARY`/`BLOB` | 用 `stsb1` 编码（§4） |
| `tags` | 高频过滤 tag 拆列 + 索引，或 `CLOB(JSON)` | 过滤下推优先 |
| `fields` | `CLOB(JSON)` | 非索引元数据 |

最终 DDL 由达梦侧决定；只要 `query()` 的可观测结果与 `MemoryBackend` 一致即可。

### 5.2 逐个 TODO 的期望行为

在 [src/core/backends/dameng_backend.cpp](../src/core/backends/dameng_backend.cpp) 中：

- `ensureConnected()`：用 `params_`（host/port/user/password/schema/driver）建连；成功置 `impl_->connected=true`；**连接失败抛 `std::runtime_error`（不是 `NotImplemented`）**。
- `createTable`：`CREATE TABLE IF NOT EXISTS <物理表>(...)`；已存在返回 `false`。
- `dropTable` / `hasTable` / `listTables`：DDL 与目录查询；`listTables` 返回**去前缀的逻辑名**，保证与 `createTable/query` round-trip。
- `insert` / `insertBatch`：**参数化绑定，禁止字符串拼接 SQL**；`value` 用 `stsb1` 编码，`timestamp` 存微秒；`insertBatch` 用数组/批量绑定 + 单事务。
- `query`：`WHERE ts BETWEEN start AND end`（闭区间、微秒），tag 过滤下推，`limit>0` 时限量，`ORDER BY ts`；`value` 用 `stsb1` 解码。
- `size` / `clear` / `flush`：`COUNT(*)` / `TRUNCATE` / 提交事务。

### 5.3 连接配置字段

通过 `StorageBackendConfig::params`（key-value）传入：

| key | 默认 | 说明 |
| --- | --- | --- |
| `host` | `127.0.0.1` | 达梦主机 |
| `port` | `5236` | 达梦默认端口 |
| `user` | `SYSDBA` | 用户名 |
| `password_env` | `DM_PASSWORD` | **存放密码的环境变量名**。密码本身不经 params，不入日志 |
| `schema` | 空 | 模式名 |
| `table_prefix` | `ts_` | 物理表名前缀 |
| `driver` | `dpi` | `dpi` 或 `odbc`（见执行计划 §4） |

> 安全：密码只从 `password_env` 指定的环境变量读取；日志与代码不出现明文（骨架的连接日志已省略密码）。

### 5.4 构建与 ABI

- 启用：`cmake -B build -S . -DSAGE_TSDB_ENABLE_DM=ON`。
- OFF（默认）时 `"dameng"` 不注册，选它会抛 `unknown backend`（无静默回退）。
- 在 [CMakeLists.txt](../CMakeLists.txt) 的 `SAGE_TSDB_ENABLE_DM` 块内有 `find_path`/`find_library`/`target_link_libraries` 的 `// TODO(DM)` 模板，接入真实驱动时填写。
- **ABI 约束**：本仓库统一 `_GLIBCXX_USE_CXX11_ABI=0`（PECJ/Torch 兼容要求）。达梦驱动库须与此 ABI 兼容，否则链接会出错。

---

## 6. 一致性测试（差分框架）

[tests/test_storage_backend_contract.cpp](../tests/test_storage_backend_contract.cpp) 提供：

- `runContractSuite(IStorageBackend&)`：后端无关的完整读写用例矩阵（建表/批量插入/闭区间范围/tag 过滤/limit/向量往返/清空/删表/缺表抛错）。
- `Stsb1Codec.*`：`stsb1` 编解码往返 + 固定字节向量断言。
- `StorageBackendContract.MemoryBackend`：对内存后端全绿（正确性基准）。
- `StorageBackendContract.DamengBackend`：`SAGE_TSDB_ENABLE_DM=ON` 时编译；驱动未填充时命中 `NotImplemented` 并 `GTEST_SKIP`（pending）。**填充后自动变为真实差分测试**，与内存后端同一套断言对拍。

运行：

```bash
# 默认（内存后端 + 编解码）
cmake -B build -S . && cmake --build build --target test_storage_backend_contract -j
./build/tests/test_storage_backend_contract

# 达梦后端（差分用例就位；填充驱动后启用）
cmake -B build -S . -DSAGE_TSDB_ENABLE_DM=ON
cmake --build build --target test_storage_backend_contract -j
./build/tests/test_storage_backend_contract   # DamengBackend 用例在填充前显示 SKIPPED
```

达梦侧完成填充后，`StorageBackendContract.DamengBackend` 应由 SKIPPED 变为 PASSED，即证明达梦后端与内存后端行为一致。

---

## 7. 变更规则

- 修改 `IStorageBackend` 方法集、时间语义、`stsb1` 格式或连接字段时，须同步更新本文与代码注释。
- 本文与代码冲突时，以代码和 `test_storage_backend_contract.cpp` 为准，并优先修正本文。
- 不新增静默 fallback 或跨后端隐式回退来掩盖未实现或错误（ADR 0001）。
