# PECJ Integrated vs Plugin Mode Benchmark

## 概述

`pecj_integrated_vs_plugin_benchmark.cpp` 用于对比 PECJ 在两种运行模式下的性能表现：

1. **融合模式 (Integrated Mode)**
   - PECJ 作为无状态计算引擎，深度集成到 sageTSDB
   - 所有数据存储在 sageTSDB 表中（stream_s, stream_r, join_results）
   - ResourceManager 管理线程和内存资源
   - 使用 `PECJComputeEngine::executeWindowJoin()` 执行窗口连接

2. **插件模式 (Plugin Mode)**
   - PECJ 作为独立插件运行，通过 `PECJAdapter` 接口调用
   - 数据通过 `feedData()` 传递，内部管理缓冲区
   - 独立线程管理或通过 ResourceHandle 提交任务
   - 使用 `process()` 触发计算并获取结果

## 对比指标

### 时间指标
| 指标 | 说明 |
|------|------|
| 总执行时间 | 从初始化到清理完成的总耗时 |
| 设置时间 | 初始化数据库、表、计算引擎/插件的时间 |
| 插入时间 | 数据插入/feed 到系统的时间 |
| 计算时间 | 执行窗口连接计算的时间 |
| 查询时间 | 从数据库查询结果的时间 |
| 清理时间 | 释放资源、重置状态的时间 |

### 资源指标
| 指标 | 说明 |
|------|------|
| 峰值内存 | 测试过程中最高内存使用量 |
| 平均内存 | 采样期间的平均内存使用量 |
| 线程使用 | 使用的工作线程数量 |
| CPU 用户态时间 | 用户态 CPU 消耗 |
| CPU 系统态时间 | 内核态 CPU 消耗 |
| 上下文切换 | 进程上下文切换次数 |

### 吞吐量指标
| 指标 | 说明 |
|------|------|
| Events/sec | 每秒处理的事件数 |
| Joins/sec | 每秒产生的连接结果数 |

## 编译

### 仅 Plugin 模式（默认）
```bash
cd build
cmake ..
make pecj_integrated_vs_plugin_benchmark
```

### 完整模式（Integrated + Plugin）
```bash
cd build
cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..
make pecj_integrated_vs_plugin_benchmark
```

## 使用方法

### 基本用法
```bash
./bin/pecj_integrated_vs_plugin_benchmark
```

### 命令行参数
```
Options:
  --s-file PATH      S 流 CSV 文件路径
  --r-file PATH      R 流 CSV 文件路径
  --events N         总事件数量 (默认: 20000)
  --threads N        线程数量 (默认: 4)
  --memory-mb N      最大内存 MB (默认: 1024)
  --window-us N      窗口长度（微秒）(默认: 10000)
  --slide-us N       滑动步长（微秒）(默认: 5000)
  --operator TYPE    算子类型: IMA, SHJ 等 (默认: IMA)
  --repeat N         重复次数 (默认: 3)
  --output FILE      输出结果到文件
  --quiet            减少输出详细程度
  --help             显示帮助信息
```

### 示例

```bash
# 使用默认配置
./bin/pecj_integrated_vs_plugin_benchmark

# 大数据量测试
./bin/pecj_integrated_vs_plugin_benchmark --events 100000 --threads 8

# 使用自定义数据集
./bin/pecj_integrated_vs_plugin_benchmark \
    --s-file /path/to/sTuple.csv \
    --r-file /path/to/rTuple.csv \
    --events 50000

# 对比不同算子
./bin/pecj_integrated_vs_plugin_benchmark --operator SHJ
./bin/pecj_integrated_vs_plugin_benchmark --operator IMA

# 输出到文件
./bin/pecj_integrated_vs_plugin_benchmark --output benchmark_results.txt
```

## 输出示例

```
╔══════════════════════════════════════════════════════════════════════════════╗
║     PECJ Performance Benchmark: Integrated Mode vs Plugin Mode               ║
║                        sageTSDB Evaluation Suite                             ║
╚══════════════════════════════════════════════════════════════════════════════╝

[Configuration]
  S Stream File    : ../../examples/datasets/sTuple.csv
  R Stream File    : ../../examples/datasets/rTuple.csv
  Event Count      : 20000
  Threads          : 4
  ...

================================================================================
  Mode: Integrated Mode (PECJComputeEngine)
================================================================================

[Data Statistics]
  Stream S Events         : 10000
  Stream R Events         : 10000
  Total Events            : 20000
  Windows Executed        : 4096
  Join Results            : 125000

[Timing Breakdown (ms)]
  Total Time              : 850.25
  Setup Time              : 12.50
  Insert Time             : 180.30
  Compute Time            : 645.20
  Query Time              : 8.75
  Cleanup Time            : 3.50

[Resource Usage]
  Peak Memory (MB)        : 128.50
  Threads Used            : 4
  CPU User Time (ms)      : 2450.00
  CPU System Time (ms)    : 85.00
  Context Switches        : 12500

[Throughput]
  Events/sec              : 23525
  Joins/sec               : 193700

================================================================================
          PECJ Performance Comparison Report
          Integrated Mode vs Plugin Mode
================================================================================

[Timing Comparison (ms)]
                        Metric      Integrated         Plugin           Diff         Winner
------------------------------------------------------------------------------------------
                    Total Time          850.25         920.50         +8.3%      Integrated
                    Setup Time           12.50          18.20        +45.6%      Integrated
                   Insert Time          180.30         195.40         +8.4%      Integrated
                  Compute Time          645.20         680.25         +5.4%      Integrated
                    Query Time            8.75          22.15       +153.1%      Integrated
                  Cleanup Time            3.50           4.50        +28.6%      Integrated

[Summary]
  Integrated Mode is 1.08x faster overall
  Integrated Mode uses 12.5% less memory

  Key Insights:
  - Integrated Mode: Deep integration with sageTSDB, optimized data path
  - Plugin Mode: More flexible, easier to swap algorithms, modular design
  - For high throughput scenarios, Integrated Mode typically performs better
  - For flexibility and maintainability, Plugin Mode is recommended
```

## 结果解读

### 何时选择 Integrated Mode
- 需要最高性能的生产环境
- 数据规模大，对延迟敏感
- 资源受限，需要精细控制内存和线程

### 何时选择 Plugin Mode
- 需要快速原型开发和测试
- 需要灵活切换不同算法
- 系统架构需要模块化设计
- 需要与其他插件（如故障检测）协同工作

## 数据文件格式

CSV 文件格式（可选，若文件不存在会自动生成合成数据）：

```csv
key,value,event_time,arrival_time
1,100.5,1000,1050
2,200.3,2000,2100
...
```

## 相关文件

- `pecj_shj_comparison_demo.cpp` - PECJ vs SHJ 算子对比（仅 Integrated 模式）
- `plugin_usage_example.cpp` - 插件系统完整使用示例
- `integrated_demo.cpp` - PECJ + 故障检测集成演示
- `performance_benchmark.cpp` - 综合性能测试

## 注意事项

1. **内存测量**：使用 `/proc/self/status` 读取 VmRSS，仅在 Linux 系统上准确
2. **重复测试**：默认运行 3 次取平均值，首次运行作为热身会被丢弃
3. **数据一致性**：两种模式使用相同的输入数据，确保对比公平
4. **资源隔离**：每次测试后会清理资源，避免状态污染

## 版本历史

- **v1.0** (2024-12): 初始版本，支持 Integrated 和 Plugin 模式对比
