# 计算引擎文档

本目录包含 sageTSDB 计算引擎相关的文档，覆盖 PECJ (Parallel Event Correlation Join) 集成和 SRTFD 无状态诊断算子。

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

### SRTFD 集成文档

4. **[SRTFD_STATELESS_OPERATOR_DESIGN.md](./SRTFD_STATELESS_OPERATOR_DESIGN.md)**
   - SRTFD 无状态算子接入设计
   - 与 PECJ deep integration 的架构映射
   - 当前实现状态、表契约和验收准则

## 相关代码

| 路径 | 说明 |
| --- | --- |
| `src/compute/pecj_compute_engine.cpp` | PECJ 计算引擎 |
| `src/compute/srtfd_compute_engine.cpp` | SRTFD 无状态诊断引擎 |
| `src/compute/window_scheduler.cpp` | 窗口调度器 |
| `src/compute/compute_state_manager.cpp` | 计算状态管理 |

- 头文件：`include/sage_tsdb/compute/`

- 示例程序：`examples/`
  - `pecj_integrated_vs_plugin_benchmark.cpp` - 性能对比
  - `integrated_demo.cpp` - 深度融合示例
  - `pecj_replay_demo.cpp` - 回放示例

## 快速开始

详细的架构设计和集成方案请参考主文档：
- [DESIGN_DOC_SAGETSDB_PECJ.md](../DESIGN_DOC_SAGETSDB_PECJ.md)
