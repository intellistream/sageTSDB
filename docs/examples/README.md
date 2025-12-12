# sageTSDB 示例程序文档

本目录包含 sageTSDB 各种示例程序的详细文档和使用指南。

## 📖 文档索引

### PECJ 集成示例

#### [Deep Integration Demo - 深度集成演示](./README_DEEP_INTEGRATION_DEMO.md)
**用途**: 展示 sageTSDB 与 PECJ 的深度集成架构

**核心特性**:
- 使用真实 PECJ 基准数据集
- 数据完全存储在 sageTSDB 表中（无 PECJ 内部缓冲区）
- PECJ 作为无状态计算引擎
- 支持多窗口并发计算
- 提供乱序模拟和大规模数据处理能力

**适用场景**:
- 评估深度集成架构性能
- 测试高乱序数据处理能力
- 大规模流式 Join 性能验证

**相关代码**: `examples/deep_integration_demo.cpp`

---

#### [High Disorder Demo - 高乱序演示](./README_HIGH_DISORDER_DEMO.md)
**用途**: 专注于高乱序、大规模数据场景的性能测试

**核心特性**:
- 可配置的乱序比例（0-100%）
- 可控的延迟范围
- 详细的乱序统计指标
- 预定义测试场景套件
- 延迟事件追踪

**适用场景**:
- 压力测试和性能极限评估
- 乱序处理能力验证
- 水印机制测试

**相关代码**: `examples/deep_integration_demo.cpp`（通过参数启用乱序模式）

**测试脚本**: `scripts/run_high_disorder_demo.sh`

---

## 🎯 选择合适的示例

### 基础功能学习
如果你是新用户，想了解 sageTSDB 的基础功能：
1. 先看 `examples/README.md` 快速开始
2. 运行 `persistence_example` 了解数据持久化
3. 运行 `table_design_demo` 了解表设计
4. 运行 `window_scheduler_demo` 了解窗口调度

### PECJ 集成学习
如果你想学习 PECJ 集成功能：
1. 先运行 `pecj_replay_demo` 了解基础流式 Join
2. 阅读 [Deep Integration Demo 文档](./README_DEEP_INTEGRATION_DEMO.md)
3. 运行 `deep_integration_demo` 体验深度集成

### 故障检测功能
如果你想了解端到端的数据处理管道：
1. 运行 `integrated_demo` 体验 PECJ + 故障检测
2. 查看实时异常告警和性能报告

### 性能评估
如果你需要进行性能评估和比较：
1. 阅读 [High Disorder Demo 文档](./README_HIGH_DISORDER_DEMO.md)
2. 使用 `scripts/run_high_disorder_demo.sh` 运行预定义测试场景
3. 使用 `performance_benchmark` 进行多维度性能对比

---

## 📚 相关文档

- [sageTSDB 设计文档](../DESIGN_DOC_SAGETSDB_PECJ.md)
- [PECJ 计算引擎实现](../PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)
- [资源管理器指南](../RESOURCE_MANAGER_GUIDE.md)
- [数据持久化](../PERSISTENCE.md)
- [表设计实现](../TABLE_DESIGN_IMPLEMENTATION.md)

---

## 🚀 快速导航

返回 [examples 目录](../../examples/) 查看所有示例代码和快速开始指南。
