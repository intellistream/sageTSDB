# 测试脚本

本目录包含 sageTSDB 的测试和性能对比脚本。

## 📚 脚本列表

### 1. compare_pecj_modes.sh
**功能**: 对比 PECJ 的插件模式 vs 集成模式性能

**对应示例**:
- `examples/benchmarks/pecj_integrated_vs_plugin_benchmark.cpp`

**用法**:
```bash
./scripts/test/compare_pecj_modes.sh
```

**对比内容**:
- **插件模式**: PECJ 作为插件运行，通过插件接口调用
- **集成模式**: PECJ 深度集成到 sageTSDB，直接调用

**输出**:
- 细粒度时间分析（Setup/Data Prep/Access/Compute/Writing）
- 时间占比分析
- 性能加速比
- JSON 格式的详细结果

---

### 2. test_lsm_tree.sh
**功能**: 测试 LSM Tree 存储引擎

**对应测试**:
- `tests/test_storage_engine.cpp`
- `tests/test_time_series_index.cpp`

**用法**:
```bash
./scripts/test/test_lsm_tree.sh
```

**测试内容**:
- ✅ LSM Tree 基础操作（Put/Get/Delete）
- ✅ 时间序列索引功能
- ✅ Compaction 机制
- ✅ 数据持久化和恢复

---

## 🎯 使用场景

### 场景 1: 评估集成模式性能优势
```bash
./compare_pecj_modes.sh
```

### 场景 2: 验证存储引擎
```bash
./test_lsm_tree.sh
```

---

## 📊 测试结果

测试结果保存位置:
```
build/test_results/           # 单元测试结果
build/fine_grained_timing.json  # 性能对比结果（JSON）
build/lsm_test.log            # LSM Tree 测试日志
```

---

## 🔍 结果分析

### 性能对比结果可视化

使用可视化工具分析 `compare_pecj_modes.sh` 的输出：

```bash
cd examples/visualization
python3 visualize_timing.py
```

生成的图表：
- `timing_comparison_bar.png` - 时间阶段对比
- `timing_stacked_bar.png` - 时间占比
- `timing_speedup.png` - 加速比分析

---

## 📖 相关文档

- [Benchmarks 说明](../../examples/benchmarks/README.md)
- [可视化工具](../../examples/visualization/README.md)
- [测试用例](../../tests/README.md)
