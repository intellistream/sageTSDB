# sageTSDB + PECJ: High Disorder & Large Scale Performance Demo

## 概述

这个demo展示了sageTSDB与PECJ深度集成在**高乱序、大规模数据**场景下的性能表现。它模拟了真实流处理系统中常见的挑战：

- 🔀 **高乱序到达**：事件不按时间顺序到达
- ⏰ **延迟事件**：部分事件延迟超过水印时间
- 📊 **大规模数据**：处理10万至50万级别的事件流
- 🔄 **滑动窗口**：多窗口并发计算
- 🧵 **多线程处理**：充分利用多核CPU

## 主要特性

### 1. 乱序模拟
- **可配置的乱序比例**：0-100%的事件可以被标记为乱序
- **可控的延迟范围**：设置最大延迟时间（微秒级）
- **真实的随机分布**：使用随机数生成器模拟实际场景
- **延迟事件统计**：追踪超过水印的事件数量

### 2. 性能指标
```
[Data Loading]
  - 数据加载速度和吞吐量
  - 数据时间跨度

[Out-of-Order Simulation]
  - 乱序事件数量和比例
  - 延迟事件数量
  - 最大/平均延迟时间

[Data Ingestion]
  - 数据插入速度和吞吐量
  - 实时进度显示

[Window Computation]
  - 窗口触发次数
  - Join结果数量
  - 计算吞吐量

[Overall Performance]
  - 端到端延迟
  - 整体吞吐量
```

### 3. 灵活配置
通过命令行参数调整所有关键参数：
- 数据规模
- 乱序比例和延迟
- 窗口大小和滑动间隔
- 线程数量
- 内存限制

## 编译

### 前置条件
```bash
# 确保PECJ已正确集成
cd /path/to/sageTSDB
./scripts/build_pecj_integrated.sh
```

### 编译demo
```bash
cd build
make deep_integration_demo -j$(nproc)
```

## 使用方法

### 方法1：使用测试套件脚本（推荐）

```bash
# 运行所有测试场景
./scripts/run_high_disorder_demo.sh all

# 运行特定场景
./scripts/run_high_disorder_demo.sh high-disorder
./scripts/run_high_disorder_demo.sh large-scale
./scripts/run_high_disorder_demo.sh quick

# 查看所有可用场景
./scripts/run_high_disorder_demo.sh help
```

#### 预定义测试场景

| 场景 | 事件数 | 乱序比例 | 最大延迟 | 说明 |
|------|--------|----------|----------|------|
| `baseline` | 200K | 0% | 0ms | 无乱序基准测试 |
| `low-disorder` | 200K | 10% | 2ms | 低乱序 |
| `medium-disorder` | 300K | 30% | 5ms | 中等乱序 |
| `high-disorder` | 400K | 50% | 10ms | 高乱序 |
| `extreme-disorder` | 400K | 70% | 20ms | 极端乱序 |
| `large-scale` | 500K | 30% | 5ms | 大规模测试 |
| `stress-test` | 600K | 60% | 15ms | 压力测试 |
| `quick` | 50K | 30% | 5ms | 快速测试 |

### 方法2：直接运行demo

```bash
cd build/examples

# 基本运行（使用默认参数）
./deep_integration_demo

# 自定义参数
./deep_integration_demo \
    --max-s 200000 \
    --max-r 200000 \
    --disorder true \
    --disorder-ratio 0.5 \
    --max-disorder-us 10000 \
    --threads 8
```

## 命令行参数

### 数据源参数
```
--s-file PATH          Stream S的CSV文件路径
--r-file PATH          Stream R的CSV文件路径
--max-s N              Stream S的最大事件数（默认：200000）
--max-r N              Stream R的最大事件数（默认：200000）
--time-unit UNIT       CSV时间单位：'ms'或'us'（默认：ms）
```

### 窗口参数
```
--window-us N          窗口长度（微秒，默认：10000 = 10ms）
--slide-us N           滑动长度（微秒，默认：5000 = 5ms）
```

### 乱序模拟参数
```
--disorder BOOL        启用乱序模拟（true/false，默认：true）
--disorder-ratio R     乱序比例 0.0-1.0（默认：0.3 = 30%）
--max-disorder-us N    最大延迟（微秒，默认：5000 = 5ms）
```

### 资源参数
```
--threads N            最大线程数（默认：8）
```

### 显示参数
```
--quiet                减少输出详细程度
--help                 显示帮助信息
```

## 示例场景

### 场景1：低乱序，验证正确性
```bash
./deep_integration_demo \
    --max-s 50000 \
    --max-r 50000 \
    --disorder true \
    --disorder-ratio 0.1 \
    --max-disorder-us 2000
```

### 场景2：中等乱序，性能测试
```bash
./deep_integration_demo \
    --max-s 150000 \
    --max-r 150000 \
    --disorder true \
    --disorder-ratio 0.3 \
    --max-disorder-us 5000 \
    --threads 8
```

### 场景3：高乱序，压力测试
```bash
./deep_integration_demo \
    --max-s 200000 \
    --max-r 200000 \
    --disorder true \
    --disorder-ratio 0.6 \
    --max-disorder-us 15000 \
    --threads 12
```

### 场景4：大规模数据
```bash
./deep_integration_demo \
    --max-s 300000 \
    --max-r 300000 \
    --disorder true \
    --disorder-ratio 0.3 \
    --max-disorder-us 5000 \
    --threads 16
```

### 场景5：对比测试（有无乱序）
```bash
# 无乱序基准
./deep_integration_demo --disorder false --max-s 100000 --max-r 100000

# 有乱序对比
./deep_integration_demo --disorder true --disorder-ratio 0.5 --max-s 100000 --max-r 100000
```

## 输出解读

### 1. 配置信息
显示当前运行的所有参数配置，包括数据规模、乱序设置、窗口参数等。

### 2. 数据加载
```
[Data Loading]
  Stream S Loaded       : 200000 events
  Stream R Loaded       : 200000 events
  Total Loaded          : 400000 events
  Load Time             : 1234 ms
  Load Throughput       : 324000 events/s
  Data Time Span        : 5000.0 ms
```

### 3. 乱序模拟统计
```
[Out-of-Order Simulation]
  Disordered Events     : 120000 (30.0%)
  Late Arrivals         : 15000 (events arriving after watermark)
  Max Disorder Delay    : 5.0 ms
  Avg Disorder Delay    : 2.5 ms
  Simulation Time       : 456 ms
```

**关键指标**：
- **Disordered Events**: 被延迟的事件总数
- **Late Arrivals**: 延迟超过水印的事件（最具挑战性）
- **Max/Avg Disorder Delay**: 实际应用的延迟时间

### 4. 数据插入性能
```
[Data Ingestion]
  Stream S Inserted     : 200000 events
  Stream R Inserted     : 200000 events
  Total Events          : 400000 events
  Insert Time           : 2345 ms
  Insert Throughput     : 170000 events/s
```

**观察点**：插入吞吐量体现了sageTSDB在高负载下的写入性能。

### 5. 窗口计算性能
```
[Window Computation]
  Windows Triggered     : 1000
  Join Results          : 456789
  Avg Results/Window    : 456
  Computation Time      : 3456 ms
  Avg per Window (us)   : 3456
  Computation Throughput: 132000 joins/s
```

**关键指标**：
- **Windows Triggered**: 处理的窗口数量
- **Join Results**: 总的Join匹配结果
- **Computation Throughput**: 体现PECJ的计算性能

### 6. 整体性能
```
[Overall Performance]
  Total Time            : 7890 ms
  Overall Throughput    : 50000 events/s
  End-to-End Latency    : 7.89 seconds
```

## 性能优化建议

### 1. 调整线程数
```bash
# 根据CPU核心数调整
--threads $(nproc)
```

### 2. 批量处理
增加数据规模以摊平启动开销：
```bash
--max-s 500000 --max-r 500000
```

### 3. 窗口大小优化
```bash
# 更大的窗口可能有更多匹配，但计算开销也更大
--window-us 20000 --slide-us 10000
```

### 4. 内存调优
在配置中增加内存限制（需要重新编译）。

## 故障排查

### 问题1：找不到CSV文件
```
❌ Failed to load data files
```
**解决**：
```bash
# 检查PECJ数据集路径
ls ../../../PECJ/benchmark/datasets/

# 或使用绝对路径
./deep_integration_demo \
    --s-file /absolute/path/to/sTuple.csv \
    --r-file /absolute/path/to/rTuple.csv
```

### 问题2：PECJ未集成
```
⚠ Stub Mode (PECJ not integrated)
```
**解决**：
```bash
cd /path/to/sageTSDB
./scripts/build_pecj_integrated.sh
```

### 问题3：性能较低
- 增加线程数：`--threads 16`
- 检查是否在Release模式编译
- 确保有足够的内存

## 扩展实验

### 1. 延迟-性能权衡
测试不同延迟下的性能变化：
```bash
for delay in 1000 2000 5000 10000 20000; do
    ./deep_integration_demo --max-disorder-us $delay --disorder-ratio 0.3
done
```

### 2. 规模测试
测试系统的扩展性：
```bash
for size in 50000 100000 200000 400000 800000; do
    ./deep_integration_demo --max-s $size --max-r $size
done
```

### 3. 线程扩展性
测试多线程效率：
```bash
for threads in 1 2 4 8 16; do
    ./deep_integration_demo --threads $threads
done
```

## 技术亮点

1. **数据库为中心的架构**
   - 所有数据存储在sageTSDB表中
   - PECJ作为无状态计算引擎
   - 清晰的职责分离

2. **真实的乱序模拟**
   - 基于随机分布的延迟注入
   - 模拟实际网络和系统延迟
   - 可控的测试场景

3. **详细的性能指标**
   - 多维度的吞吐量统计
   - 延迟分布分析
   - 实时进度反馈

4. **可扩展的测试框架**
   - 灵活的命令行配置
   - 预定义测试套件
   - 易于添加新的测试场景

## 相关文档

- [PECJ Integration Guide](../docs/PECJ_DEEP_INTEGRATION_SUMMARY.md)
- [Resource Manager Guide](../docs/RESOURCE_MANAGER_GUIDE.md)
- [Performance Tuning](../docs/PERFORMANCE_TUNING.md)

## 联系与反馈

如有问题或建议，请提交Issue或联系开发团队。
