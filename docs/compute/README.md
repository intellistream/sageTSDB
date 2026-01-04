# 计算引擎文档

本目录包含 sageTSDB 计算引擎相关的文档，主要是 PECJ (Parallel Event Correlation Join) 集成。

## 文档列表

### PECJ 集成文档

1. **[PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md](./PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)**
   - PECJComputeEngine 核心实现
   - 配置说明和使用指南
   - 性能优化建议
   - 故障处理方法

2. **[PECJ_OPERATORS_INTEGRATION.md](./PECJ_OPERATORS_INTEGRATION.md)**
   - 支持的 PECJ 算子列表（IAWJ, MeanAQP, IMA, MSWJ 等）
   - 各算子的特性和使用场景
   - 算子配置参数说明

3. **[PECJ_BENCHMARK_README.md](./PECJ_BENCHMARK_README.md)**
   - 融合模式 vs 插件模式性能对比
   - 基准测试指标说明
   - 编译和运行指南

## 相关代码

- 实现代码：`src/compute/`
  - `pecj_compute_engine.cpp` - PECJ 计算引擎
  - `window_scheduler.cpp` - 窗口调度器
  - `compute_state_manager.cpp` - 计算状态管理

- 头文件：`include/sage_tsdb/compute/`

- 示例程序：`examples/`
  - `pecj_integrated_vs_plugin_benchmark.cpp` - 性能对比
  - `integrated_demo.cpp` - 深度融合示例
  - `pecj_replay_demo.cpp` - 回放示例

## 快速开始

详细的架构设计和集成方案请参考主文档：
- [DESIGN_DOC_SAGETSDB_PECJ.md](../DESIGN_DOC_SAGETSDB_PECJ.md)
