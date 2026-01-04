# 算法模块文档

本目录包含 sageTSDB 流处理算法的文档。

## 算法列表

### 1. 流式 Join (Stream Join)

**实现文件**：`src/algorithms/stream_join.cpp`

**功能**：
- 基于时间窗口的流式 Join 操作
- 支持乱序数据处理
- 窗口类型：滚动窗口 (Tumbling)、滑动窗口 (Sliding)

**使用场景**：
- 多流关联分析
- 事件相关性检测
- 实时数据融合

### 2. 窗口聚合 (Window Aggregation)

**实现文件**：`src/algorithms/window_aggregator.cpp`

**功能**：
- 时间窗口内的聚合计算
- 支持多种聚合函数：SUM, AVG, COUNT, MIN, MAX
- 增量更新和高效计算

**使用场景**：
- 实时统计分析
- 监控指标计算
- 时序数据汇总

### 3. 算法基类

**实现文件**：`src/algorithms/algorithm_base.cpp`

**功能**：
- 提供算法的统一接口
- 定义算法生命周期管理
- 配置和参数管理

## 相关代码

- 实现代码：`src/algorithms/`
- 头文件：`include/sage_tsdb/algorithms/`
- 测试代码：`tests/test_stream_join.cpp`, `tests/test_window_aggregator.cpp`

## 扩展新算法

如需添加新的流处理算法：

1. 继承 `AlgorithmBase` 基类
2. 实现核心算法逻辑
3. 提供配置接口
4. 编写单元测试和性能测试
5. 更新本文档

## 算法性能

各算法的性能指标和优化建议请参考：
- `examples/performance_benchmark.cpp` - 性能基准测试
- [DESIGN_DOC_SAGETSDB_PECJ.md](../DESIGN_DOC_SAGETSDB_PECJ.md) - 性能优化章节

## PECJ 集成

sageTSDB 通过 PECJ 计算引擎提供更强大的流式 Join 算法（IAWJ, SHJ, MSWJ 等）。

详细信息请参考：
- [../compute/](../compute/) - PECJ 计算引擎文档
- [../compute/PECJ_OPERATORS_INTEGRATION.md](../compute/PECJ_OPERATORS_INTEGRATION.md) - PECJ 算子说明
