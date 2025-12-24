# sageTSDB Copilot Instructions

## Overview

**sageTSDB** 是高性能时序数据库，C++20 编写，支持乱序数据处理、窗口计算和 PECJ 算法深度融合。作为 SAGE 项目的子模块，位于 `packages/sage-middleware/src/sage/middleware/components/sage_tsdb/`。

## Architecture (Database-Centric Design)

```
include/sage_tsdb/         # 公共头文件
├── core/                  # 核心存储: TimeSeriesDB, LSM-Tree, TableManager
├── compute/               # PECJ计算引擎: PECJComputeEngine, WindowScheduler
├── algorithms/            # 流处理算法: StreamJoin, WindowAggregator
├── plugins/               # 插件系统: PluginManager, PECJAdapter
└── utils/                 # 工具类: CSVDataLoader, Config

src/                       # 实现文件 (结构同 include/)
tests/                     # GoogleTest 单元测试
examples/                  # Demo 程序 (persistence, PECJ 集成等)
```

**关键设计原则**：
- **数据存储优先**：所有数据必须先写入 `TimeSeriesDB` 表 (MemTable/LSM-Tree)，PECJ 从表查询
- **PECJ 无状态化**：`PECJComputeEngine` 是纯计算函数，不持有数据缓冲区
- **资源统一管理**：线程/内存/GPU 由 `ResourceManager` 分配，通过 `ResourceHandle` 使用

## Build Commands

```bash
# 基本构建 (独立模式)
./scripts/build.sh

# 构建并测试
./scripts/build.sh --test

# PECJ 深度融合模式构建
cmake -B build -S . \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR=/path/to/PECJ
cmake --build build -j$(nproc)

# 运行测试
cd build && ctest --output-on-failure
```

**构建选项**：
- `-DPECJ_MODE=PLUGIN`: 插件模式 (传统, 作为 baseline)
- `-DPECJ_MODE=INTEGRATED`: 深度融合模式 (生产推荐)
- `-DBUILD_PYTHON_BINDINGS=ON`: Python pybind11 绑定

## Dual-Mode Architecture

### Plugin Mode (Baseline)
```cpp
// 数据直接进入 PECJ 内部队列
PECJAdapter adapter;
adapter.feedData(data);  // PECJ 维护自己的缓冲区和线程
```

### Integrated Mode (Production)
```cpp
// 数据必须先存入 DB 表
TimeSeriesDB db;
db.createTable("stream_s", TableType::Stream);
db.insert("stream_s", data);  // 唯一数据入口

// PECJ 从表查询计算
PECJComputeEngine engine(&db);
engine.executeWindowJoin(window_id, time_range);

// 结果从表查询
auto results = db.query("join_results", query_params);
```

## Code Patterns

### Namespace 结构
```cpp
namespace sage_tsdb {           // 顶层命名空间
namespace core { }              // 核心存储组件
namespace compute { }           // 计算引擎
}
```

### 资源申请模式
```cpp
ResourceRequest request;
request.requested_threads = 4;
request.max_memory_bytes = 100 * 1024 * 1024;

auto handle = resource_manager->allocate("my_plugin", request);
handle->submitTask([&]() { /* 异步任务 */ });
handle->reportUsage(usage);  // 定期报告使用情况
```

### 窗口调度模式
```cpp
WindowScheduler scheduler(config);
scheduler.setCallback([](const WindowContext& ctx) {
    // 窗口触发时调用
    engine.executeWindowJoin(ctx.window_id, ctx.time_range);
});
scheduler.start();
```

## Key Files

| 文件 | 用途 |
|-----|------|
| `include/sage_tsdb/core/time_series_db.h` | 主数据库 API (createTable, insert, query) |
| `include/sage_tsdb/compute/pecj_compute_engine.h` | PECJ 无状态计算引擎 |
| `include/sage_tsdb/core/resource_manager.h` | 资源统一管理 |
| `include/sage_tsdb/plugins/plugin_interface.h` | 插件接口定义 |
| `examples/deep_integration_demo.cpp` | 深度融合架构演示 |

## Testing

```bash
# 单个测试
./build/tests/test_time_series_db

# GoogleTest filter
./build/tests/test_pecj_compute_engine --gtest_filter="*WindowJoin*"

# 全量测试
cd build && ctest -V
```

测试命名规范：`test_<component>.cpp`，使用 GoogleTest 框架。

## ABI Compatibility

**重要**：PECJ/PyTorch 使用旧 ABI，所有编译必须添加：
```cmake
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)
```

## Demo 运行

```bash
cd build/examples
source setup_env.sh  # 配置 LD_LIBRARY_PATH

# 深度融合演示
./deep_integration_demo --events-s 5000 --events-r 5000 --keys 100

# PECJ 数据重放
./pecj_replay_demo --s-file /path/to/sTuple.csv --r-file /path/to/rTuple.csv
```

## Documentation

- 架构设计: `docs/DESIGN_DOC_SAGETSDB_PECJ.md`
- 深度融合总结: `docs/PECJ_DEEP_INTEGRATION_SUMMARY.md`
- 资源管理: `docs/RESOURCE_MANAGER_GUIDE.md`
- LSM-Tree 实现: `docs/LSM_TREE_IMPLEMENTATION.md`
