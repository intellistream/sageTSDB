# sageTSDB 脚本工具

本目录包含用于构建、测试和运行 sageTSDB 示例的各种脚本工具。

---

## 📋 脚本分类索引

### 🔨 构建脚本
- [build.sh](#buildsh) - 主构建脚本（所有示例）
- [build_and_test.sh](#build_and_testsh) - 构建并测试（所有示例）
- [build_plugins.sh](#build_pluginssh) - 插件系统构建

### 🎮 演示运行脚本
- [run_demo.sh](#run_demosh) - 交互式演示启动器
- [run_high_disorder_demo.sh](#run_high_disorder_demosh) - 高乱序场景测试
- [demo_disorder_showcase.sh](#demo_disorder_showcasesh) - 乱序能力展示
- [benchmark_disorder.sh](#benchmark_disordersh) - 乱序性能基准测试

### 🧪 测试和对比脚本
- [compare_pecj_modes.sh](#compare_pecj_modessh) - PECJ 集成模式对比
- [test_lsm_tree.sh](#test_lsm_treesh) - LSM Tree 存储引擎测试

---

## 🔨 构建脚本

### build.sh

**功能**: 主构建脚本，适用于所有示例程序

**对应示例**: 
- ✅ 所有 `examples/` 中的示例程序

**用法**:
```bash
# 基本构建
./scripts/build.sh

# 构建并运行测试
./scripts/build.sh --test

# 构建并安装
./scripts/build.sh --install
```

**特性**:
- 自动检测构建目录（SAGE 统一构建或本地构建）
- 配置 CMake 并编译项目
- 支持测试和安装选项
- 启用 PECJ 深度集成模式

**生成的可执行文件位置**: `build/examples/`

---

### build_and_test.sh

**功能**: 构建并运行完整的示例测试，包含验证步骤

**对应示例**:
- ✅ `persistence_example.cpp` - 持久化测试
- ✅ `table_design_demo.cpp` - 表设计测试
- ✅ `pecj_replay_demo.cpp` - PECJ Join 测试
- ✅ `integrated_demo.cpp` - 端到端集成测试

**用法**:
```bash
./scripts/build_and_test.sh
```

**特性**:
- 检查前置条件（CMake、g++、PECJ 库）
- 编译所有示例程序
- 运行快速验证测试
- 生成测试报告

**输出**: 控制台日志和测试报告

---

### build_plugins.sh

**功能**: 构建插件系统和相关示例

**对应示例**:
- 🎯 **主要**: `plugin_usage_example.cpp` - 插件系统使用演示
- ✅ 其他所有核心库和插件

**用法**:
```bash
./scripts/build_plugins.sh
```

**环境变量**:
- `PECJ_DIR`: PECJ 库路径（可选，自动检测）

**特性**:
- 自动检测 PECJ 库位置
- 支持有/无 PECJ 的构建
- 编译核心库和所有插件

---

## 🎮 演示运行脚本

### run_demo.sh

**功能**: 交互式演示启动器，支持多种预配置场景

**对应示例**:
- 🎯 **主要**: `pecj_replay_demo.cpp` - 基础流 Join 演示
- 🎯 **主要**: `integrated_demo.cpp` - PECJ + 故障检测集成
- 🎯 **主要**: `performance_benchmark.cpp` - 性能基准测试
- ✅ `deep_integration_demo.cpp` - 高级集成演示（可选）

**用法**:
```bash
# 交互式菜单
./scripts/run_demo.sh

# 直接运行特定演示
./scripts/run_demo.sh integrated
./scripts/run_demo.sh pecj
./scripts/run_demo.sh performance
```

**支持的演示场景**:
1. **Basic Replay Demo** - 基础流 Join（5 分钟）→ `pecj_replay_demo`
2. **Integrated Demo** - PECJ + 故障检测（10 分钟）→ `integrated_demo`
3. **Performance Benchmark** - 性能基准（15-30 分钟）→ `performance_benchmark`
4. **Stock Data Demo** - 股票数据演示 → `pecj_replay_demo` (with stock data)
5. **High Throughput Demo** - 高吞吐量演示 → `pecj_replay_demo` (SHJ operator)
6. **Realtime Simulation** - 实时模拟演示 → `pecj_replay_demo` (timestamp replay)

**前置条件**:
- 已构建项目（运行 `build.sh`）
- PECJ 数据集可用（`PECJ/benchmark/datasets/`）

---

### run_high_disorder_demo.sh

**功能**: 高乱序场景测试套件，展示系统处理乱序事件的能力

**对应示例**:
- 🎯 **主要**: `deep_integration_demo.cpp` - 深度集成乱序处理演示

**用法**:
```bash
# 运行所有测试场景（推荐）
./scripts/run_high_disorder_demo.sh all

# 运行特定场景
./scripts/run_high_disorder_demo.sh baseline      # 无乱序基线
./scripts/run_high_disorder_demo.sh low-disorder  # 低乱序 (10%)
./scripts/run_high_disorder_demo.sh med-disorder  # 中乱序 (30%)
./scripts/run_high_disorder_demo.sh high-disorder # 高乱序 (50%)
./scripts/run_high_disorder_demo.sh extreme       # 极端乱序 (70%)
./scripts/run_high_disorder_demo.sh large-scale   # 大规模测试 (100K+ events)
```

**测试场景**:
- **Baseline**: 无乱序，建立性能基线
- **Low Disorder**: 10% 乱序率，2ms 最大延迟
- **Medium Disorder**: 30% 乱序率，5ms 最大延迟
- **High Disorder**: 50% 乱序率，10ms 最大延迟
- **Extreme Disorder**: 70% 乱序率，20ms 最大延迟
- **Large Scale**: 100K+ 事件，30% 乱序率

**输出指标**:
- 总处理时间和吞吐量
- 乱序事件数量和延迟分布
- 迟到事件统计（超过水位线）
- 窗口触发数量和 Join 结果数

**适用场景**: 性能评估、压力测试、技术演示

---

### demo_disorder_showcase.sh

**功能**: 快速展示高乱序和大规模处理能力（简化版）

**对应示例**:
- 🎯 **主要**: `deep_integration_demo.cpp` - 深度集成乱序处理演示

**用法**:
```bash
./scripts/demo_disorder_showcase.sh
```

**演示内容**:
1. **Baseline Performance** - 50K 事件，无乱序
2. **Medium Disorder** - 50K 事件，30% 乱序，5ms 延迟
3. **High Disorder** - 50K 事件，50% 乱序，10ms 延迟
4. **Large Scale** - 100K 事件，30% 乱序

**特点**:
- 交互式演示，每个场景后暂停
- 实时显示处理进度和统计信息
- 友好的可视化输出（彩色终端）
- 适合向非技术人员展示

**运行时间**: 约 10-15 分钟

---

### benchmark_disorder.sh

**功能**: 系统化的乱序性能基准测试，生成详细报告

**对应示例**:
- 🎯 **主要**: `deep_integration_demo.cpp` - 深度集成乱序处理演示

**用法**:
```bash
./scripts/benchmark_disorder.sh
```

**测试矩阵**:
- **事件规模**: 10K, 50K, 100K, 200K
- **乱序比例**: 0%, 10%, 30%, 50%, 70%
- **最大延迟**: 0us, 1000us, 5000us, 10000us, 20000us

**输出文件**:
- `build/benchmark_results/disorder_benchmark_YYYYMMDD_HHMMSS.csv` - 详细数据
- `build/benchmark_results/disorder_benchmark_YYYYMMDD_HHMMSS_summary.txt` - 摘要报告

**CSV 字段**:
```
Scenario, Events, DisorderRatio, MaxDelayUs, TotalTimeMs, LoadThroughput, 
InsertThroughput, ComputeTimeMs, DisoreredEvents, LateArrivals, MaxDisorderMs, 
AvgDisorderMs, Windows, JoinResults
```

**适用场景**:
- 性能评估和对比
- 论文实验数据收集
- 系统调优参考

**运行时间**: 约 30-60 分钟（取决于测试组合）

---

## 🧪 测试和对比脚本

### compare_pecj_modes.sh

**功能**: 对比 PECJ 插件模式（PLUGIN）和深度集成模式（INTEGRATED）的性能差异

**对应示例**:
- 🎯 **主要**: `pecj_integrated_vs_plugin_benchmark.cpp` - PECJ 模式性能对比
- ✅ `pecj_replay_demo.cpp` - 用于两种模式的测试

**用法**:
```bash
# 使用默认配置
./scripts/compare_pecj_modes.sh

# 指定 PECJ 路径
PECJ_DIR=/path/to/PECJ ./scripts/compare_pecj_modes.sh

# 指定构建类型
BUILD_TYPE=Debug ./scripts/compare_pecj_modes.sh
```

**环境变量**:
- `PECJ_DIR`: PECJ 库路径（默认: `/home/cdb/dameng/PECJ`）
- `BUILD_TYPE`: 构建类型（默认: `Release`）
- `NUM_JOBS`: 并行编译任务数（默认: CPU 核心数）

**测试步骤**:
1. 构建 PLUGIN 模式
2. 构建 INTEGRATED 模式
3. 运行性能对比测试
4. 生成对比报告

**对比指标**:
- 内存占用（RSS, Shared Memory）
- 执行时间
- 吞吐量（events/s）
- API 调用开销

**输出**: 
- 控制台对比表格
- 详细日志文件

**适用场景**: 
- 架构决策支持
- 性能优化验证
- 技术文档编写

---

### test_lsm_tree.sh

**功能**: LSM Tree 存储引擎的专项测试

**对应测试文件**:
- 🎯 **主要**: `tests/test_storage_engine.cpp` - LSM Tree 单元测试
- ✅ `tests/test_time_series_db.cpp` - 时序数据库测试

**用法**:
```bash
./scripts/test_lsm_tree.sh
```

**测试内容**:
1. 编译项目
2. 运行所有单元测试
3. LSM Tree 性能测试（10K 数据点）
4. 存储结构验证

**输出指标**:
- 测试通过率
- 写入时间（ms）
- 读取时间（ms）
- SSTable 文件数量
- WAL 日志状态

**前置条件**: 需要先运行 `build.sh` 完成构建

**适用场景**: 
- 存储引擎开发和调试
- 持久化功能验证
- 性能回归测试

---

## 📊 脚本使用流程图

```
┌─────────────────┐
│  初次使用？     │
└────────┬────────┘
         │
         ├─ 是 ──→ 1. ./scripts/build.sh
         │         2. ./scripts/run_demo.sh (选择 Demo 1)
         │
         ├─ 否 ──→ 根据需求选择：
         │
         ├─ 开发测试 ──→ ./scripts/build_and_test.sh
         │
         ├─ 性能评估 ──→ ./scripts/run_high_disorder_demo.sh all
         │              ./scripts/benchmark_disorder.sh
         │
         ├─ 功能演示 ──→ ./scripts/demo_disorder_showcase.sh
         │
         ├─ 架构对比 ──→ ./scripts/compare_pecj_modes.sh
         │
         └─ 存储测试 ──→ ./scripts/test_lsm_tree.sh
```

---

## 🚀 快速开始指南

### 场景 1: 首次使用（5 分钟）

```bash
# 1. 构建项目
cd /path/to/sageTSDB
./scripts/build.sh

# 2. 运行第一个演示
./scripts/run_demo.sh
# 选择 "1) Basic Replay Demo"
```

### 场景 2: 完整功能演示（15 分钟）

```bash
# 运行预配置的乱序展示
./scripts/demo_disorder_showcase.sh
```

### 场景 3: 性能评估（30 分钟）

```bash
# 运行完整的乱序测试套件
./scripts/run_high_disorder_demo.sh all

# 或运行系统化的基准测试
./scripts/benchmark_disorder.sh
```

### 场景 4: 开发和调试

```bash
# 构建并运行所有测试
./scripts/build_and_test.sh

# 测试 LSM Tree 存储引擎
./scripts/test_lsm_tree.sh
```

---

## 📝 示例程序与脚本对应关系总览

| 示例程序 | 对应脚本 | 功能说明 |
|---------|---------|---------|
| **persistence_example.cpp** | `build_and_test.sh` | 持久化和检查点测试 |
| **plugin_usage_example.cpp** | `build_plugins.sh` | 插件系统使用演示 |
| **table_design_demo.cpp** | `build_and_test.sh` | 表设计和数据操作 |
| **window_scheduler_demo.cpp** | `build.sh` | 窗口调度机制 |
| **pecj_replay_demo.cpp** | `run_demo.sh` | 基础流 Join 演示 |
| **integrated_demo.cpp** | `run_demo.sh` | PECJ + 故障检测集成 |
| **performance_benchmark.cpp** | `run_demo.sh` | 性能基准测试 |
| **deep_integration_demo.cpp** | `run_high_disorder_demo.sh`<br>`demo_disorder_showcase.sh`<br>`benchmark_disorder.sh` | 深度集成乱序处理 |
| **pecj_integrated_vs_plugin_benchmark.cpp** | `compare_pecj_modes.sh` | PECJ 模式性能对比 |
| **pecj_shj_comparison_demo.cpp** | `build.sh` | PECJ 算子对比 |

**测试文件**:
| 测试文件 | 对应脚本 | 功能说明 |
|---------|---------|---------|
| **test_storage_engine.cpp** | `test_lsm_tree.sh` | LSM Tree 存储引擎测试 |
| **test_*.cpp** (其他测试) | `build_and_test.sh` | 各种单元测试 |

---

## 🔧 常见问题

### Q: 如何查看脚本的详细用法？
**A**: 大多数脚本支持 `--help` 参数，或直接查看脚本内部的注释：
```bash
./scripts/run_demo.sh --help
head -n 50 ./scripts/run_high_disorder_demo.sh
```

### Q: PECJ 数据集在哪里？
**A**: 脚本会自动搜索以下位置：
- `../../../PECJ/benchmark/datasets/` (相对于 sageTSDB)
- `./examples/datasets/` (本地拷贝)
- `/home/cdb/dameng/PECJ/benchmark/datasets/` (绝对路径)

你也可以在运行演示时手动指定：
```bash
./build/examples/pecj_replay_demo --s-file /path/to/sTuple.csv --r-file /path/to/rTuple.csv
```

### Q: 构建失败怎么办？
**A**: 检查以下几点：
1. CMake 版本 ≥ 3.15
2. g++ 支持 C++17
3. PECJ 库路径正确：`PECJ_DIR=/path/to/PECJ ./scripts/build.sh`
4. 查看详细错误日志

### Q: 如何并行运行多个测试？
**A**: 不建议并行运行脚本，因为它们可能共享构建目录。如需并行测试，为每个测试创建独立的构建目录：
```bash
BUILD_DIR=build_test1 ./scripts/build.sh
BUILD_DIR=build_test2 ./scripts/build.sh
```

### Q: 如何清理构建产物？
**A**: 
```bash
# 清理构建目录
rm -rf build/

# 清理测试数据
rm -rf build/sage_tsdb_data/

# 清理基准测试结果
rm -rf build/benchmark_results/
```

---

## 📖 相关文档

- **[示例程序文档](../examples/README.md)** - 示例程序详细说明
- **[Deep Integration Demo](../docs/examples/README_DEEP_INTEGRATION_DEMO.md)** - 深度集成演示文档
- **[High Disorder Demo](../docs/examples/README_HIGH_DISORDER_DEMO.md)** - 高乱序测试文档
- **[sageTSDB 设计文档](../docs/DESIGN_DOC_SAGETSDB_PECJ.md)** - 系统设计文档
- **[PECJ 计算引擎](../docs/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)** - PECJ 集成实现

---

## 🤝 贡献指南

如果你添加了新的脚本：

1. **命名规范**: 使用描述性名称，如 `run_xxx_demo.sh` 或 `test_xxx.sh`
2. **脚本头部**: 添加清晰的注释说明功能、用法和参数
3. **更新文档**: 在本 README 中添加相应条目
4. **标注对应示例**: 明确指出脚本对应的 `examples/` 文件
5. **测试**: 确保脚本在干净环境下可以正常运行

**脚本模板**:
```bash
#!/bin/bash
# <script_name>.sh
# 功能说明
#
# 对应示例: examples/<demo_name>.cpp
#
# 用法:
#   ./scripts/<script_name>.sh [options]
#
# 参数:
#   --option1    说明
#   --option2    说明

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# ... 脚本内容 ...
```

---

## 📞 获取帮助

- 查看 [完整文档](../docs/)
- 查看源代码中的详细注释
- 提交 Issue 或 Pull Request

---

**最后更新**: 2025-12-29  
**维护者**: sageTSDB 开发团队
