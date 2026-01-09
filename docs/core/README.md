# 核心模块文档

本目录包含 sageTSDB 核心模块的文档，涵盖存储引擎、资源管理、表设计等关键组件。

## 文档列表

### 存储引擎

1. **[LSM_TREE_IMPLEMENTATION.md](./LSM_TREE_IMPLEMENTATION.md)**
   - LSM Tree 存储引擎实现细节
   - MemTable、Immutable MemTable、SSTable 设计
   - 数据结构和压缩策略
   - WAL (Write-Ahead Log) 机制

2. **[PERSISTENCE.md](./PERSISTENCE.md)**
   - 数据持久化功能完整指南
   - 保存/加载、检查点管理
   - 增量写入和恢复机制
   - API 使用示例和最佳实践
   - 性能优化建议

### 表设计

3. **[TABLE_DESIGN_IMPLEMENTATION.md](./TABLE_DESIGN_IMPLEMENTATION.md)**
   - StreamTable 和 JoinResultTable 实现
   - TableManager 多表管理
   - 表结构设计和索引机制
   - 窗口查询和聚合统计

### 资源管理

4. **[RESOURCE_MANAGER_GUIDE.md](./RESOURCE_MANAGER_GUIDE.md)**
   - 资源管理器使用指南
   - 线程池和内存管理
   - 资源分配和监控
   - 性能调优建议

## 相关代码

- 实现代码：`src/core/`
  - `lsm_tree.cpp` - LSM Tree 存储引擎
  - `storage_engine.cpp` - 存储引擎封装
  - `stream_table.cpp` - 流输入表
  - `join_result_table.cpp` - Join 结果表
  - `table_manager.cpp` - 表管理器
  - `resource_manager.cpp` - 资源管理器
  - `time_series_db.cpp` - 时序数据库主类
  - `time_series_index.cpp` - 时间序列索引

- 头文件：`include/sage_tsdb/core/`

## 架构概述

核心模块是 sageTSDB 的基础设施层，提供：

- **数据存储**：高效的 LSM Tree 存储引擎，支持乱序写入和快速查询
- **表管理**：多表设计，支持流输入表和 Join 结果表
- **资源管理**：统一的线程池、内存管理和资源调度
- **持久化**：完整的数据持久化和恢复机制

详细的架构设计请参考主文档：
- [DESIGN_DOC_SAGETSDB_PECJ.md](../DESIGN_DOC_SAGETSDB_PECJ.md)
