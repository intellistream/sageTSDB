# sageTSDB 文档索引

本目录包含 sageTSDB 时序数据库的完整技术文档，按模块分类组织。

## 📖 主文档

**[DESIGN_DOC_SAGETSDB_PECJ.md](./DESIGN_DOC_SAGETSDB_PECJ.md)**
- sageTSDB 与 PECJ 集成的详细设计文档
- 总体架构、双模式设计、运行机制
- 数据流和计算引擎设计
- **推荐首先阅读此文档以了解整体架构**

## 📂 模块文档

文档已按照 `src/` 目录结构重新组织，方便查找和维护。

### 🔧 核心模块 (core/)

**[core/README.md](./core/README.md)** - 核心模块总览

- **[core/LSM_TREE_IMPLEMENTATION.md](./core/LSM_TREE_IMPLEMENTATION.md)**
  - LSM Tree 存储引擎实现
  - MemTable、SSTable、WAL 机制

- **[core/PERSISTENCE.md](./core/PERSISTENCE.md)**
  - 数据持久化功能指南
  - 保存/加载、检查点、增量写入

- **[core/TABLE_DESIGN_IMPLEMENTATION.md](./core/TABLE_DESIGN_IMPLEMENTATION.md)**
  - StreamTable 和 JoinResultTable 实现
  - TableManager 多表管理

- **[core/RESOURCE_MANAGER_GUIDE.md](./core/RESOURCE_MANAGER_GUIDE.md)**
  - 资源管理器使用指南
  - 线程池和内存管理

### ⚙️ 计算引擎 (compute/)

**[compute/README.md](./compute/README.md)** - 计算引擎总览

- **[compute/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md](./compute/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)**
  - PECJComputeEngine 实现详解
  - 配置和性能优化

- **[compute/PECJ_OPERATORS_INTEGRATION.md](./compute/PECJ_OPERATORS_INTEGRATION.md)**
  - PECJ 算子集成（IAWJ, MeanAQP, IMA, MSWJ 等）
  - 算子特性和使用场景

- **[compute/PECJ_BENCHMARK_README.md](./compute/PECJ_BENCHMARK_README.md)**
  - 融合模式 vs 插件模式性能对比
  - 基准测试指南

### 🔌 插件系统 (plugins/)

**[plugins/README.md](./plugins/README.md)** - 插件系统总览
- 插件模式 vs 融合模式
- 插件开发指南

### 🧮 算法模块 (algorithms/)

**[algorithms/README.md](./algorithms/README.md)** - 算法模块总览
- 流式 Join
- 窗口聚合
- 算法扩展指南

### 🛠️ 工具模块 (utils/)

**[utils/README.md](./utils/README.md)** - 工具模块总览
- 配置管理
- 日志系统
- 性能监控

## 🚀 快速导航

### 新手入门
1. 阅读 [DESIGN_DOC_SAGETSDB_PECJ.md](./DESIGN_DOC_SAGETSDB_PECJ.md) 了解架构
2. 查看 [core/PERSISTENCE.md](./core/PERSISTENCE.md) 学习基本使用
3. 运行 `examples/` 目录中的示例程序

### 开发者指南
1. [DESIGN_DOC_SAGETSDB_PECJ.md](./DESIGN_DOC_SAGETSDB_PECJ.md) - 理解整体设计
2. 根据需要查看对应模块的文档
3. 参考 `tests/` 目录中的单元测试

### 性能优化
1. [compute/PECJ_BENCHMARK_README.md](./compute/PECJ_BENCHMARK_README.md) - 性能对比
2. [core/RESOURCE_MANAGER_GUIDE.md](./core/RESOURCE_MANAGER_GUIDE.md) - 资源调优
3. [compute/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md](./compute/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md) - 计算优化

## 📝 文档组织原则

- 按照 `src/` 源代码目录结构组织文档
- 每个模块目录包含 `README.md` 作为总览
- 详细文档放在对应模块目录下
- 主设计文档 `DESIGN_DOC_SAGETSDB_PECJ.md` 保留在根目录

## 🔗 相关资源

- **示例代码**：`../examples/` - 各种使用示例
- **单元测试**：`../tests/` - 完整的测试用例
- **构建脚本**：`../scripts/` - 编译和测试脚本
- **主 README**：`../README.md` - 项目总览
2. `RESOURCE_MANAGER_GUIDE.md` - 管理资源分配
3. `LSM_TREE_IMPLEMENTATION.md` - 优化存储性能

### 故障排除
- 性能问题 → `RESOURCE_MANAGER_GUIDE.md`
- 存储问题 → `LSM_TREE_IMPLEMENTATION.md` + `PERSISTENCE.md`
- 集成问题 → `DESIGN_DOC_SAGETSDB_PECJ.md`

## 其他资源

- **测试用例**: `tests/` 目录包含完整的单元测试，可作为 API 使用参考
- **示例代码**: `examples/` 目录包含各种使用场景的示例
- **API 文档**: 使用 Doxygen 生成完整的 API 文档（运行 `doxygen Doxyfile`）

## 文档贡献

如果您发现文档有误或需要补充，欢迎提交 Pull Request 或 Issue。
