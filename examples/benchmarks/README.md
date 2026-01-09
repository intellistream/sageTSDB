# 性能测试 (Benchmarks)

本目录包含 sageTSDB 和 PECJ 集成的性能测试程序，用于评估系统在不同场景下的表现。

## 📚 测试程序列表

### 1. performance_benchmark.cpp
**功能**: 多维度性能评估

**测试内容**:
- 吞吐量测试（不同数据规模）
- 延迟测试（P50/P95/P99）
- 内存使用分析
- CPU 利用率监测
- 多种 Join 算法对比

**运行时间**: 15-30 分钟

**运行方式**:
```bash
cd build/examples
./performance_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --output-json ../../examples/outputs/results/benchmark_results.json
```

**输出**: JSON 格式的详细性能指标

---

### 2. pecj_integrated_vs_plugin_benchmark.cpp
**功能**: 集成模式 vs 插件模式性能对比

**测试内容**:
- **集成模式**: PECJ 深度集成到 sageTSDB
- **插件模式**: PECJ 作为插件运行
- 细粒度时间分析（Setup/Data Prep/Access/Compute/Writing）
- 时间占比分析
- 性能瓶颈识别

**运行时间**: ~10 分钟

**运行方式**:
```bash
cd build/examples
./pecj_integrated_vs_plugin_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --output-json ../../examples/outputs/results/fine_grained_timing.json
```

**配套脚本**:
```bash
# 运行测试并生成可视化图表
cd examples/visualization
./test_fine_grained_timing.sh
```

---

## 📊 配置文件

### configs/demo_configs.json
包含预定义的测试配置：
- 数据规模配置
- Join 算法选择
- 乱序率设置
- 窗口大小配置

**使用方式**:
```bash
./performance_benchmark --config configs/demo_configs.json
```

---

## 🎯 使用场景

### 场景 1: 快速性能评估
运行 `pecj_integrated_vs_plugin_benchmark`，快速了解系统性能

### 场景 2: 全面性能测试
运行 `performance_benchmark`，获取完整的性能报告

### 场景 3: 性能调优
1. 运行 benchmark 获取基线数据
2. 调整配置参数
3. 重新运行对比性能变化

---

## 📈 结果可视化

所有 benchmark 输出 JSON 文件，可使用可视化工具生成图表：

```bash
cd ../visualization
python3 visualize_benchmark.py ../outputs/results/benchmark_results.json
python3 visualize_timing.py  # 生成细粒度时间分析图
```

图表输出位置: `../outputs/figures/`

---

## 📖 相关文档

- [可视化工具](../visualization/README.md)
- [PECJ Benchmark 详细说明](../../docs/compute/PECJ_BENCHMARK_README.md)
- [性能优化指南](../../docs/)
