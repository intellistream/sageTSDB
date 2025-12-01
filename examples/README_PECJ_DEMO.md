# sageTSDB + PECJ Demo 使用指南

本目录包含三个完整的演示程序，展示 sageTSDB 与 PECJ 的集成能力。这些 demo 使用真实的 PECJ 数据集，适合向客户和利益相关者展示系统功能。

## 📦 Demo 列表

### 1. PECJ 重放 Demo (`pecj_replay_demo`)
**用途**: 展示基础的流式 Join 功能

**特性**:
- 从真实数据集加载 S 流和 R 流
- 按到达时间顺序重放数据
- 实时显示窗口触发和 Join 结果
- 统计吞吐量、延迟等性能指标
- 支持多种 PECJ 算子（IMA、MSWJ、SHJ 等）

**运行方式**:
```bash
cd build
./examples/pecj_replay_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 10000 \
    --operator IMA \
    --window-ms 1000
```

**输出示例**:
```
╔══════════════════════════════════════════════════════════════════════════╗
║                   sageTSDB + PECJ Integration Demo                       ║
║                   Real-Time Stream Join with PECJ                        ║
╚══════════════════════════════════════════════════════════════════════════╝

[Configuration]
  S Stream File     : ../../../PECJ/benchmark/datasets/sTuple.csv
  R Stream File     : ../../../PECJ/benchmark/datasets/rTuple.csv
  Max Tuples        : 10000
  PECJ Operator     : IMA
  Window Length     : 1000 ms
  ...

[Performance Statistics]
  Total Tuples Processed    : 10000
  Windows Triggered         : 145
  Join Results Generated    : 8523
  Throughput (K tuples/sec) : 45.32
  Join Selectivity (%)      : 85.23
```

---

### 2. 集成 Demo (`integrated_demo`)
**用途**: 展示 PECJ + 故障检测的完整数据管道

**特性**:
- 同时运行 PECJ 和故障检测插件
- 实时检测 Join 结果中的异常
- 生成告警日志
- 输出完整的性能报告（文本和文件）
- 支持 Z-Score 和 VAE 检测方法

**运行方式**:
```bash
cd build
./examples/integrated_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 10000 \
    --detection zscore \
    --threshold 3.0 \
    --output integrated_results.txt
```

**输出示例**:
```
╔══════════════════════════════════════════════════════════════════════════╗
║             sageTSDB Integrated Demo: PECJ + Fault Detection             ║
║                   Real-Time Stream Join with Anomaly Detection           ║
╚══════════════════════════════════════════════════════════════════════════╝

[Processing Stream]
  Progress: 20% (2000/10000)
  [ALERT] Anomaly at t=1234567890, value=125.67, score=3.45
  Progress: 40% (4000/10000)
  ...

[Performance Report]
  Total Tuples Processed    : 10000
  Anomalies Detected        : 23
  Detection Rate            : 0.23%
  Throughput (K tuples/sec) : 42.15
```

---

### 3. 性能基准测试 (`performance_benchmark`)
**用途**: 系统性能评估和对比

**特性**:
- 测试多种 PECJ 算子（IMA、SHJ、MSWJ）
- 评估不同数据规模的性能
- 对比不同线程数的效果
- 多次重复测试取平均值
- 生成 CSV 格式的结果报告

**运行方式**:
```bash
cd build
./examples/performance_benchmark \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --output benchmark_results.csv
```

**输出示例**:
```
╔══════════════════════════════════════════════════════════════════════════╗
║                  sageTSDB Performance Benchmark Suite                    ║
║                      PECJ Algorithm Evaluation                           ║
╚══════════════════════════════════════════════════════════════════════════╝

Benchmark Results Summary
═══════════════════════════════════════════════════════════════════════════
Operator    Tuples      Threads   Throughput(K/s)     Latency(ms)    Windows
IMA         1000        1         25 ± 2              40.12          15
IMA         1000        4         85 ± 5              11.76          15
SHJ         1000        4         92 ± 3              10.87          15
MSWJ        1000        4         78 ± 4              12.82          15
...
```

---

## 🚀 快速开始

### 前置条件
1. 已构建 sageTSDB 和 PECJ
2. PECJ 数据集位于 `PECJ/benchmark/datasets/`
3. 已安装必要的依赖（参见主 README）

### 构建 Demo
```bash
cd /path/to/sageTSDB
mkdir -p build && cd build

# 配置（启用 PECJ 集成）
cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON ..

# 构建
make -j$(nproc)

# 验证可执行文件
ls examples/pecj_replay_demo
ls examples/integrated_demo
ls examples/performance_benchmark
```

### 运行示例
```bash
# 基础演示（快速）
./examples/pecj_replay_demo --max-tuples 5000

# 集成演示（推荐）
./examples/integrated_demo --max-tuples 10000 --detection zscore

# 性能测试（需要较长时间）
./examples/performance_benchmark
```

---

## 📊 数据集说明

### PECJ 数据集格式
所有数据集遵循以下 CSV 格式：
```csv
key,value,eventTime,arrivalTime
51209364,1,0,455000
86971226,1,0,455000
...
```

**字段说明**:
- `key`: Join 键（用于匹配 S 流和 R 流）
- `value`: 元组值
- `eventTime`: 事件时间戳（微秒）
- `arrivalTime`: 到达时间戳（微秒，用于模拟乱序）

### 可用数据集
位于 `PECJ/benchmark/datasets/`:
- **sTuple.csv / rTuple.csv**: 通用测试数据（~60K / ~77K 条）
- **stock/**: 股票交易数据（多个延迟级别）
- **retail/**: 零售交易数据
- **rovio/**: Rovio 游戏数据
- **logistics/**: 物流数据

### 自定义数据集
可以使用自己的数据集，只需确保：
1. 遵循上述 CSV 格式
2. 包含表头行
3. 时间戳单位为微秒

---

## 🎯 演示场景建议

### 场景 1: 基础功能展示（5 分钟）
**目标**: 展示系统能正常工作

```bash
./examples/pecj_replay_demo --max-tuples 5000
```

**演示要点**:
- 数据加载速度
- 实时处理进度
- 最终统计报告（吞吐量、延迟）

---

### 场景 2: 完整管道展示（10 分钟）
**目标**: 展示从数据输入到异常检测的端到端能力

```bash
./examples/integrated_demo --max-tuples 10000 --detection zscore --threshold 2.5
```

**演示要点**:
- 双插件协同工作
- 实时告警输出
- 检测到的异常数量和比例
- 报告文件生成

---

### 场景 3: 性能对比（15 分钟）
**目标**: 展示不同算子和配置的性能差异

```bash
./examples/performance_benchmark
```

**演示要点**:
- IMA vs SHJ vs MSWJ 的性能对比
- 多线程加速效果
- 数据规模对性能的影响
- CSV 报告可用于进一步分析

---

## ⚙️ 配置选项详解

### 通用选项
| 选项 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `--s-file` | S 流数据文件路径 | `../../../PECJ/benchmark/datasets/sTuple.csv` | `--s-file /path/to/s.csv` |
| `--r-file` | R 流数据文件路径 | `../../../PECJ/benchmark/datasets/rTuple.csv` | `--r-file /path/to/r.csv` |
| `--max-tuples` | 最大处理元组数 | 全部 | `--max-tuples 10000` |
| `--help` | 显示帮助信息 | - | `--help` |

### PECJ 特定选项
| 选项 | 说明 | 默认值 | 可选值 |
|------|------|--------|--------|
| `--operator` | PECJ 算子类型 | IMA | IMA, SHJ, MSWJ, AI, LinearSVI, PRJ |
| `--window-ms` | 窗口长度（毫秒） | 1000 | 任意正整数 |
| `--realtime` | 按真实时间戳重放 | false | - |

### 故障检测选项
| 选项 | 说明 | 默认值 | 可选值 |
|------|------|--------|--------|
| `--detection` | 检测方法 | zscore | zscore, vae, hybrid |
| `--threshold` | 异常阈值 | 3.0 | 任意浮点数 |
| `--output` | 输出文件路径 | `integrated_demo_results.txt` | 任意路径 |

---

## 📈 性能预期

### 测试环境
- CPU: Intel i7-9700K (8 cores)
- RAM: 16 GB
- OS: Ubuntu 20.04

### 典型性能指标
| 配置 | 吞吐量 (K tuples/sec) | 延迟 (ms) | 内存占用 (MB) |
|------|----------------------|-----------|---------------|
| IMA, 1 线程 | 25-30 | 35-45 | 150-200 |
| IMA, 4 线程 | 80-90 | 10-15 | 200-300 |
| SHJ, 4 线程 | 90-100 | 9-12 | 180-250 |
| MSWJ, 4 线程 | 75-85 | 12-18 | 250-350 |

**注意**: 实际性能受数据特征（乱序程度、键分布）影响较大。

---

## 🐛 故障排查

### 问题 1: 找不到数据文件
```
[ERROR] Failed to open file: ../../../PECJ/benchmark/datasets/sTuple.csv
```
**解决方案**:
- 确认 PECJ 目录位置
- 使用绝对路径：`--s-file /absolute/path/to/sTuple.csv`
- 检查文件权限

### 问题 2: PECJ 插件初始化失败
```
[ERROR] Failed to initialize PECJ: ...
```
**解决方案**:
- 确认编译时启用了 `PECJ_FULL_INTEGRATION`
- 检查 PECJ 库是否正确链接
- 查看 CMake 配置日志

### 问题 3: 吞吐量异常低
**可能原因**:
- 数据集过小（使用 `--max-tuples` 增加）
- 磁盘 I/O 瓶颈（使用 SSD）
- 线程配置不当（增加 `--threads`）

---

## 📝 输出文件说明

### integrated_demo_results.txt
包含完整的运行报告：
- 配置参数
- 处理统计
- 故障检测结果
- 告警日志
- 性能指标

### benchmark_results.csv
CSV 格式的性能测试结果，包含：
- Operator: 算子名称
- TupleCount: 元组数量
- ThreadCount: 线程数
- AvgThroughput_KTps: 平均吞吐量
- AvgLatency_ms: 平均延迟
- Windows: 触发的窗口数
- JoinResults: Join 结果数

可用 Excel、Python (pandas) 等工具进一步分析。

---

## 🎨 可视化建议

### 使用 Python 绘制性能图表
```python
import pandas as pd
import matplotlib.pyplot as plt

# 读取基准测试结果
df = pd.read_csv('benchmark_results.csv')

# 吞吐量对比
df_grouped = df.groupby('Operator')['AvgThroughput_KTps'].mean()
df_grouped.plot(kind='bar', title='PECJ Operator Throughput Comparison')
plt.ylabel('Throughput (K tuples/sec)')
plt.savefig('throughput_comparison.png')

# 线程扩展性
df_threads = df[df['Operator'] == 'IMA']
plt.figure()
plt.plot(df_threads['ThreadCount'], df_threads['AvgThroughput_KTps'], marker='o')
plt.xlabel('Thread Count')
plt.ylabel('Throughput (K tuples/sec)')
plt.title('IMA Scalability')
plt.savefig('scalability.png')
```

---

## 🔗 相关文档

- [PECJ 原始文档](../../../PECJ/README.md)
- [sageTSDB 架构设计](../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [插件开发指南](../docs/PLUGIN_DEVELOPMENT.md)
- [API 参考](../docs/API_REFERENCE.md)

---

## 📧 联系与支持

如有问题或建议，请联系：
- 项目主页: [GitHub - sageTSDB](https://github.com/intellistream/sageTSDB)
- Issue 跟踪: [GitHub Issues](https://github.com/intellistream/sageTSDB/issues)
- 邮件列表: sagetsdb@intellistream.org

---

## 📄 许可证

本 demo 遵循与 sageTSDB 主项目相同的许可证。详见 [LICENSE](../LICENSE) 文件。

---

**最后更新**: 2025-12-01  
**版本**: 1.0.0
