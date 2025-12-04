# sageTSDB 文档索引

本目录包含 sageTSDB 时序数据库的完整技术文档。

## 文档列表

### 核心设计文档

1. **[DESIGN_DOC_SAGETSDB_PECJ.md](./DESIGN_DOC_SAGETSDB_PECJ.md)**
   - sageTSDB 与 PECJ 集成的详细设计文档
   - 总体架构、运行机制、多线程模型
   - 插件系统设计（PluginManager、EventBus）
   - PECJAdapter 和 FaultDetectionAdapter 实现细节
   - 数据流和事件处理机制
   - 适合：理解系统整体设计和工作原理

### 功能实现文档

2. **[PERSISTENCE.md](./PERSISTENCE.md)**
   - 数据持久化功能完整指南
   - 包括：保存/加载、检查点管理、增量写入
   - API 使用示例和最佳实践
   - 性能优化建议
   - 适合：需要使用持久化功能的开发者

3. **[LSM_TREE_IMPLEMENTATION.md](./LSM_TREE_IMPLEMENTATION.md)**
   - LSM Tree 存储引擎实现文档
   - 数据结构设计、压缩策略
   - WAL（Write-Ahead Log）机制
   - 适合：理解底层存储机制的开发者

### 组件指南

4. **[RESOURCE_MANAGER_GUIDE.md](./RESOURCE_MANAGER_GUIDE.md)**
   - 资源管理器使用指南
   - 线程池和内存管理
   - 资源分配和监控
   - 适合：需要优化资源使用的开发者

## 快速导航

### 新手入门
如果您是第一次使用 sageTSDB，建议按以下顺序阅读：
1. `DESIGN_DOC_SAGETSDB_PECJ.md` - 了解系统架构
2. `PERSISTENCE.md` - 学习基本使用
3. 参考 `examples/` 目录中的示例代码

### 进阶开发
如果您需要扩展或修改 sageTSDB：
1. `DESIGN_DOC_SAGETSDB_PECJ.md` - 理解插件系统
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
