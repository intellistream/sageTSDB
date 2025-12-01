# PECJ + sageTSDB Demo 快速开始

## 🚀 一键运行

```bash
# 进入 sageTSDB 目录
cd /path/to/sageTSDB

# 运行交互式菜单
./examples/run_demo.sh

# 或者快速运行基础 demo
./examples/run_demo.sh --quick
```

## 📋 Demo 概览

| Demo | 用途 | 时间 | 适合场景 |
|------|------|------|---------|
| **Basic Replay** | 展示流 Join 基础功能 | ~5 分钟 | 快速演示 |
| **Integrated Pipeline** | 展示 PECJ + 故障检测 | ~10 分钟 | 完整功能展示 |
| **Performance Benchmark** | 性能评估和对比 | 15-30 分钟 | 技术评估 |

## 🎯 推荐演示流程

### 场景 1: 向客户快速展示（5 分钟）
```bash
./examples/run_demo.sh --quick
```
展示要点：数据加载 → 实时处理 → 性能统计

### 场景 2: 完整功能演示（10 分钟）
```bash
./examples/run_demo.sh
# 选择: 2) Integrated Demo
```
展示要点：双流 Join → 异常检测 → 实时告警 → 报告生成

### 场景 3: 技术评估（30 分钟）
```bash
./examples/run_demo.sh
# 选择: 3) Performance Benchmark
```
展示要点：多算子对比 → 扩展性测试 → 性能图表

## 📊 预期输出

### 控制台输出示例
```
╔══════════════════════════════════════════════════════════════════════════╗
║                   sageTSDB + PECJ Integration Demo                       ║
╚══════════════════════════════════════════════════════════════════════════╝

[Loading Data]
  Loading S stream ... OK (5000 tuples)
  Loading R stream ... OK (5000 tuples)

[Replaying Data Stream]
  Progress: [====================] 100% (10000/10000)

[Performance Statistics]
  Total Tuples Processed    : 10000
  Throughput (K tuples/sec) : 45.32
  Join Selectivity (%)      : 85.23
```

### 生成的文件
- `integrated_results.txt` - 完整运行报告
- `benchmark_results.csv` - 性能测试结果（可用于图表）

## 🔧 自定义运行

### 使用不同的数据集
```bash
cd build
./examples/pecj_replay_demo \
    --s-file /path/to/your/s_stream.csv \
    --r-file /path/to/your/r_stream.csv \
    --max-tuples 20000
```

### 调整 PECJ 算子
```bash
./examples/pecj_replay_demo \
    --operator SHJ \      # 或 IMA, MSWJ, AI 等
    --window-ms 500 \
    --max-tuples 10000
```

### 调整故障检测参数
```bash
./examples/integrated_demo \
    --detection zscore \  # 或 vae
    --threshold 2.5 \
    --max-tuples 10000
```

## 📖 更多信息

详细文档请查看：
- **完整文档**: `examples/README_PECJ_DEMO.md`
- **配置文件**: `examples/demo_configs.json`
- **架构设计**: `docs/DESIGN_DOC_SAGETSDB_PECJ.md`

## ❓ 常见问题

**Q: 数据文件找不到？**
```bash
# 使用绝对路径
./examples/pecj_replay_demo --s-file /absolute/path/to/sTuple.csv --r-file /absolute/path/to/rTuple.csv
```

**Q: 性能低于预期？**
```bash
# 增加线程数（需重新编译，修改配置中的 threads）
# 或减少数据量
./examples/pecj_replay_demo --max-tuples 5000
```

**Q: 如何查看所有选项？**
```bash
./examples/pecj_replay_demo --help
./examples/integrated_demo --help
./examples/performance_benchmark --help
```

## 🎨 可视化结果

使用 Python 绘制性能图表：
```python
import pandas as pd
import matplotlib.pyplot as plt

# 读取基准测试结果
df = pd.read_csv('benchmark_results.csv')

# 绘制吞吐量对比图
df.groupby('Operator')['AvgThroughput_KTps'].mean().plot(kind='bar')
plt.title('PECJ Operator Throughput Comparison')
plt.ylabel('Throughput (K tuples/sec)')
plt.savefig('throughput_comparison.png')
```

## 📧 获取帮助

- GitHub Issues: https://github.com/intellistream/sageTSDB/issues
- 邮件: sagetsdb@intellistream.org
- 文档: `examples/README_PECJ_DEMO.md`

---

**提示**: 首次运行建议使用 `--max-tuples 5000` 快速验证系统是否正常工作。
