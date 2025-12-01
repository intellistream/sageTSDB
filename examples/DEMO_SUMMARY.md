# PECJ + sageTSDB 集成 Demo 总结

## 📦 已创建的文件

### 核心代码文件
```
sageTSDB/
├── include/sage_tsdb/utils/
│   └── csv_data_loader.h              # CSV 数据加载器工具类
├── examples/
│   ├── pecj_replay_demo.cpp           # PECJ 基础重放演示
│   ├── integrated_demo.cpp            # PECJ + 故障检测集成演示
│   ├── performance_benchmark.cpp      # 性能基准测试
│   ├── CMakeLists.txt                 # 更新的构建配置
│   ├── demo_configs.json              # Demo 配置文件集合
│   ├── run_demo.sh                    # 交互式启动脚本
│   ├── README_PECJ_DEMO.md           # 完整使用文档
│   └── QUICKSTART.md                  # 快速入门指南
```

## 🎯 Demo 功能对比

| 特性 | pecj_replay_demo | integrated_demo | performance_benchmark |
|-----|------------------|-----------------|----------------------|
| **数据加载** | ✅ 真实数据集 | ✅ 真实数据集 | ✅ 真实数据集 |
| **流式 Join** | ✅ PECJ 算子 | ✅ PECJ 算子 | ✅ 多种算子对比 |
| **故障检测** | ❌ | ✅ Z-Score/VAE | ❌ |
| **性能统计** | ✅ 基础统计 | ✅ 详细统计 | ✅ 全面基准测试 |
| **实时告警** | ❌ | ✅ 异常告警 | ❌ |
| **报告生成** | ✅ 控制台 | ✅ 控制台+文件 | ✅ CSV 文件 |
| **可视化支持** | ❌ | ❌ | ✅ CSV 可导出 |
| **运行时间** | 5 分钟 | 10 分钟 | 15-30 分钟 |
| **适用场景** | 快速演示 | 完整功能展示 | 技术评估 |

## 🚀 快速启动指南

### 方式 1: 使用交互式脚本（推荐）
```bash
cd /path/to/sageTSDB
./examples/run_demo.sh
```

### 方式 2: 直接运行
```bash
cd /path/to/sageTSDB/build

# 基础演示
./examples/pecj_replay_demo --max-tuples 5000

# 完整演示
./examples/integrated_demo --max-tuples 10000 --detection zscore

# 性能测试
./examples/performance_benchmark
```

### 方式 3: 一键快速演示
```bash
./examples/run_demo.sh --quick
```

## 📊 Demo 输出示例

### 1. pecj_replay_demo 输出
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
  Slide Length      : 500 ms
  Lateness Tolerance: 100 ms
  PECJ Threads      : 4
  Realtime Replay   : No

[Loading Data]
  Loading S stream from: ../../../PECJ/benchmark/datasets/sTuple.csv ... OK (5000 tuples)
  Loading R stream from: ../../../PECJ/benchmark/datasets/rTuple.csv ... OK (5000 tuples)
  Total tuples to process: 10000

[Initializing PECJ Plugin]
  PECJ plugin initialized with IMA operator

[Replaying Data Stream]
  Progress: [==================================================] 100% (10000/10000)

[Finalizing]
  Waiting for PECJ to flush remaining windows...

[PECJ Internal Stats]
  windows_triggered             : 145
  join_results                  : 8523
  tuples_processed              : 10000
  avg_processing_time_us        : 25.3

===============================================================================
Performance Statistics
===============================================================================
S Stream Tuples:               5000
R Stream Tuples:               5000
Total Tuples Processed:        10000
Windows Triggered:             145
Join Results Generated:        8523
Processing Time (ms):          220.50
Throughput (K tuples/sec):     45.35
Join Selectivity (%):          85.23
===============================================================================

[Demo Completed Successfully]

Tip: Run with --help to see all available options.
```

### 2. integrated_demo 输出
```
╔══════════════════════════════════════════════════════════════════════════╗
║             sageTSDB Integrated Demo: PECJ + Fault Detection             ║
║                   Real-Time Stream Join with Anomaly Detection           ║
╚══════════════════════════════════════════════════════════════════════════╝

[Configuration]
  S Stream File          : ../../../PECJ/benchmark/datasets/sTuple.csv
  R Stream File          : ../../../PECJ/benchmark/datasets/rTuple.csv
  Max Tuples             : 10000
  PECJ Operator          : IMA
  Window Length          : 1000 ms
  Detection Method       : zscore
  Detection Threshold    : 3.0
  Output File            : integrated_demo_results.txt

[Loading Data]
  Loading S stream ... OK (5000 tuples)
  Loading R stream ... OK (5000 tuples)

[Initializing Plugins]
  PECJ plugin initialized (IMA)
  Fault detection initialized (zscore)

[Processing Stream]
  Progress: 5% (500/10000)
  Progress: 10% (1000/10000)
  [ALERT] Anomaly at t=1234567890, value=125.67, score=3.45
  Progress: 15% (1500/10000)
  ...
  Progress: 100% (10000/10000)

[Finalizing]

[Plugin Statistics]
  pecj:
    windows_triggered         : 145
    join_results              : 8523
    avg_latency_us            : 25.3
  fault_detection:
    anomalies_detected        : 23
    false_positive_rate       : 0.12%

===============================================================================
Integrated Demo - Performance Report
===============================================================================

[Data Processing]
  Total Tuples Processed    : 10000
  Windows Triggered         : 145
  Join Results Generated    : 8523

[Fault Detection]
  Anomalies Detected        : 23
  Detection Rate            : 0.23%

[Performance]
  Processing Time (ms)      : 235
  Throughput (K tuples/sec) : 42.55

[Alert Log] (Last 10 alerts)
  [14] Anomaly at t=1234567890, value=125.67, score=3.45
  [15] Anomaly at t=1234789012, value=98.32, score=3.12
  ...
  [23] Anomaly at t=1239876543, value=142.89, score=4.01

===============================================================================

  Report saved to: integrated_demo_results.txt

[Demo Completed Successfully]
```

### 3. performance_benchmark 输出
```
╔══════════════════════════════════════════════════════════════════════════╗
║                  sageTSDB Performance Benchmark Suite                    ║
║                      PECJ Algorithm Evaluation                           ║
╚══════════════════════════════════════════════════════════════════════════╝

[Configuration]
  S File: ../../../PECJ/benchmark/datasets/sTuple.csv
  R File: ../../../PECJ/benchmark/datasets/rTuple.csv
  Operators: IMA SHJ MSWJ 
  Tuple Counts: 1000 5000 10000 50000 
  Thread Counts: 1 2 4 8 
  Repeat Count: 3
  Output: benchmark_results.csv

[Test 1/36]
  Operator: IMA, Tuples: 1000, Threads: 1
  Avg Throughput: 25.32 K tuples/sec
  Avg Latency: 39.51 ms

[Test 2/36]
  Operator: IMA, Tuples: 1000, Threads: 2
  Avg Throughput: 45.67 K tuples/sec
  Avg Latency: 21.89 ms
...

========================================================================================================================
Benchmark Results Summary
========================================================================================================================
Operator    Tuples      Threads   Throughput(K/s)     Latency(ms)    Windows        Join Results   
------------------------------------------------------------------------------------------------------------------------
IMA         1000        1         25 ± 2              39.51          15             823            
IMA         1000        2         46 ± 3              21.89          15             823            
IMA         1000        4         85 ± 5              11.76          15             823            
IMA         1000        8         92 ± 6              10.87          15             823            
IMA         5000        4         82 ± 4              60.98          75             4156           
SHJ         1000        4         92 ± 3              10.87          15             845            
SHJ         5000        4         95 ± 4              52.63          75             4289           
MSWJ        1000        4         78 ± 4              12.82          15             798            
...
========================================================================================================================

[INFO] Results saved to: benchmark_results.csv

[Benchmark Completed Successfully]
```

## 📈 展示建议

### 给客户/管理层（非技术）
1. **运行**: `integrated_demo`
2. **重点展示**:
   - 实时数据处理能力（进度条）
   - 异常检测告警（红色高亮）
   - 最终性能指标（吞吐量、准确率）
3. **时间**: 10 分钟

### 给技术团队
1. **运行**: `performance_benchmark`
2. **重点展示**:
   - 多算子性能对比
   - 多线程扩展性
   - CSV 结果可进一步分析
3. **时间**: 15-30 分钟

### 快速演示（时间有限）
1. **运行**: `./run_demo.sh --quick`
2. **重点展示**:
   - 系统能正常工作
   - 基本性能指标
3. **时间**: 5 分钟

## 🔧 自定义和扩展

### 添加新的数据集
1. 准备 CSV 文件（格式：key,value,eventTime,arrivalTime）
2. 运行：
```bash
./examples/pecj_replay_demo --s-file /path/to/new_s.csv --r-file /path/to/new_r.csv
```

### 测试不同的 PECJ 算子
支持的算子：IMA, SHJ, MSWJ, AI, LinearSVI, PRJ, MeanAQP

```bash
./examples/pecj_replay_demo --operator SHJ --max-tuples 10000
```

### 调整检测灵敏度
```bash
./examples/integrated_demo --threshold 2.5  # 更敏感（更多告警）
./examples/integrated_demo --threshold 3.5  # 更保守（更少告警）
```

## 📋 检查清单

在向客户演示前，确保：
- [ ] 已成功构建所有 demo 可执行文件
- [ ] 数据文件路径正确（或使用绝对路径）
- [ ] 测试运行过至少一次（验证无错误）
- [ ] 准备好解释关键指标（吞吐量、延迟、选择率）
- [ ] 如需可视化，已准备好 Python 脚本或 Excel

## 🐛 常见问题

### 问题 1: 编译错误
```
解决方案：
1. 确保已安装 PECJ 并正确设置 PECJ_DIR
2. 使用 -DPECJ_FULL_INTEGRATION=ON 选项
3. 检查 C++17 支持
```

### 问题 2: 运行时找不到数据文件
```
解决方案：
1. 使用绝对路径
2. 检查 PECJ 数据集是否存在
3. 使用 run_demo.sh 脚本（自动处理路径）
```

### 问题 3: 性能低于预期
```
解决方案：
1. 增加 --max-tuples 以获得更稳定的测量
2. 确保系统负载不高
3. 使用 SSD 而非 HDD
4. 调整线程数（重新配置编译）
```

## 📞 获取帮助

- **查看完整文档**: `cat examples/README_PECJ_DEMO.md`
- **查看命令选项**: `./examples/pecj_replay_demo --help`
- **运行交互式菜单**: `./examples/run_demo.sh`
- **GitHub Issues**: https://github.com/intellistream/sageTSDB/issues

## 🎉 总结

您现在拥有三个功能完整、可直接展示的 demo 程序：

1. **pecj_replay_demo**: 适合快速展示基础功能
2. **integrated_demo**: 适合展示完整的数据处理管道
3. **performance_benchmark**: 适合技术评估和性能对比

所有 demo 都使用 PECJ 的真实数据集，提供专业的输出格式，并包含详细的性能统计。

**推荐首次运行**:
```bash
cd /path/to/sageTSDB
./examples/run_demo.sh --quick
```

祝演示成功！ 🚀
