# 演示脚本

本目录包含 sageTSDB 示例程序的演示运行脚本。

## 📚 脚本列表

### 1. run_demo.sh
**功能**: 交互式演示启动器，提供多种预配置演示场景

**对应示例**:
- `examples/integration/pecj_replay_demo.cpp` - 基础流 Join
- `examples/integration/integrated_demo.cpp` - 端到端集成
- `examples/benchmarks/performance_benchmark.cpp` - 性能测试

**用法**:
```bash
./scripts/demo/run_demo.sh
```

**演示场景**:
1. Basic Replay Demo - 基础流 Join 演示（5分钟）
2. Integrated Demo - PECJ + 故障检测（10分钟）
3. Performance Benchmark - 性能基准测试（15-30分钟）
4. Stock Data Demo - 股票数据演示
5. High Throughput Demo - 高吞吐量演示
6. Realtime Simulation - 实时模拟

---

### 2. run_disorder_demo.sh
**功能**: 乱序处理能力演示和性能测试

**对应示例**:
- `examples/integration/deep_integration_demo.cpp`

**用法**:
```bash
# 交互式菜单
./scripts/demo/run_disorder_demo.sh

# 直接运行特定模式
./scripts/demo/run_disorder_demo.sh quick      # 快速演示（5分钟）
./scripts/demo/run_disorder_demo.sh full       # 完整测试（15分钟）
./scripts/demo/run_disorder_demo.sh benchmark  # 性能基准（30分钟）
./scripts/demo/run_disorder_demo.sh all        # 运行所有模式
```

**演示内容**:
- Quick: 测试 10%, 20%, 30% 乱序率
- Full: 测试 10%, 20%, 30%, 50% 乱序率，多种数据规模
- Benchmark: 全面性能基准测试，生成 CSV 报告

**合并自**: `benchmark_disorder.sh`, `demo_disorder_showcase.sh`, `run_high_disorder_demo.sh`

---

### 3. run_pecj_shj_comparison.sh
**功能**: PECJ (IMA) 与 SHJ (Symmetric Hash Join) 算法对比

**对应示例**:
- `examples/integration/pecj_shj_comparison_demo.cpp`

**用法**:
```bash
./scripts/demo/run_pecj_shj_comparison.sh
```

**对比内容**:
- 小规模 vs 大规模数据
- 流模式 vs 批处理模式
- 多窗口时间序列 Join 计算

---

## 🎯 使用场景

### 场景 1: 快速展示系统能力（推荐）
```bash
./run_demo.sh
# 选择 "1) Basic Replay Demo"
```

### 场景 2: 演示乱序处理能力
```bash
./run_disorder_demo.sh quick
```

### 场景 3: 算法对比演示
```bash
./run_pecj_shj_comparison.sh
```

### 场景 4: 性能评估
```bash
./run_demo.sh
# 选择 "3) Performance Benchmark"

# 或
./run_disorder_demo.sh benchmark
```

---

## 📊 输出结果

所有演示脚本的输出默认保存在:
```
build/disorder_results/      # 乱序测试结果
build/benchmark_results/     # 性能测试结果
build/*.txt                  # 其他演示结果
```

---

## 📖 相关文档

- [示例程序目录](../../examples/)
- [Integration 示例说明](../../examples/integration/README.md)
- [Benchmarks 说明](../../examples/benchmarks/README.md)
