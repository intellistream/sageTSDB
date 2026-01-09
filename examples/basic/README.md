# 基础功能示例

本目录包含 sageTSDB 的基础功能示例程序，适合新用户学习核心概念。

## 📚 示例列表

### 1. persistence_example.cpp
**功能**: 数据持久化和检查点管理

演示内容：
- 时间序列数据的创建和插入
- 检查点创建和数据持久化
- 从检查点恢复数据
- 数据完整性验证

**运行时间**: ~2 分钟

**运行方式**:
```bash
cd build/examples
./persistence_example
```

---

### 2. table_design_demo.cpp
**功能**: 表设计和基础数据操作

演示内容：
- 时间序列表的创建和配置
- 数据插入操作
- 时间范围查询
- 聚合查询

**运行时间**: ~1 分钟

**运行方式**:
```bash
cd build/examples
./table_design_demo
```

---

### 3. window_scheduler_demo.cpp
**功能**: 窗口调度和触发机制

演示内容：
- 滑动窗口和滚动窗口配置
- 基于时间和基于计数的触发策略
- 窗口聚合计算
- 窗口状态管理

**运行时间**: ~2 分钟

**运行方式**:
```bash
cd build/examples
./window_scheduler_demo
```

---

## 🎯 学习路径建议

**第一步**: 先运行 `table_design_demo`，了解基本的表操作

**第二步**: 运行 `window_scheduler_demo`，学习窗口计算

**第三步**: 运行 `persistence_example`，理解数据持久化

---

## 📖 相关文档

- [sageTSDB 快速入门](../../QUICKSTART.md)
- [完整 API 文档](../../docs/)
- [高级功能示例](../integration/)
