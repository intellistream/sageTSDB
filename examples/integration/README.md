# PECJ 集成示例

本目录包含 sageTSDB 与 PECJ (乱序流式 Join) 引擎的集成示例，展示端到端的流数据处理能力。

## 📚 示例列表

### 1. pecj_replay_demo.cpp
**功能**: 基础流式 Join 数据重放演示

**演示内容**:
- 从 CSV 文件读取流数据
- 执行基础的流式 Join 操作
- 支持多种 Join 算法 (IMA, SHJ, MWAY, PMJAM)
- 性能指标输出

**运行时间**: ~5 分钟

**运行方式**:
```bash
cd build/examples
./pecj_replay_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 5000 \
    --operator IMA
```

**参数说明**:
- `--s-file`: S 流数据文件路径
- `--r-file`: R 流数据文件路径
- `--max-tuples`: 最大处理元组数
- `--operator`: Join 算法 (IMA/SHJ/MWAY/PMJAM)

---

### 2. pecj_shj_comparison_demo.cpp
**功能**: PECJ vs SHJ 算法对比

**演示内容**:
- PECJ 和传统 SHJ 算法性能对比
- 不同乱序率下的性能表现
- 内存使用对比
- 吞吐量和延迟对比

**运行时间**: ~8 分钟

**运行方式**:
```bash
cd build/examples
./pecj_shj_comparison_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv
```

---

### 3. integrated_demo.cpp
**功能**: PECJ + 故障检测端到端演示

**演示内容**:
- 流式数据接入
- 实时 Join 计算
- 故障检测插件集成
- 完整的数据处理管道

**运行时间**: ~10 分钟

**运行方式**:
```bash
cd build/examples
./integrated_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv
```

---

### 4. deep_integration_demo.cpp
**功能**: 深度集成架构和乱序处理

**演示内容**:
- PECJ 深度集成模式
- 高级乱序处理机制
- 自适应缓冲区管理
- 细粒度性能分析

**运行时间**: ~15 分钟

**运行方式**:
```bash
cd build/examples
./deep_integration_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --disorder-rate 0.3
```

**参数说明**:
- `--disorder-rate`: 乱序率 (0.0-1.0)

---

## 🎯 学习路径建议

**入门**: `pecj_replay_demo` → 理解基本流式 Join

**进阶**: `pecj_shj_comparison_demo` → 了解算法对比

**实战**: `integrated_demo` → 完整数据处理管道

**深入**: `deep_integration_demo` → 高级特性和优化

---

## 📖 相关文档

- [PECJ 设计文档](../../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [PECJ Benchmark 说明](../../docs/compute/PECJ_BENCHMARK_README.md)
- [性能测试](../benchmarks/)
