# 插件系统文档

本目录包含 sageTSDB 插件系统的文档。

## 插件架构

sageTSDB 支持两种模式：

1. **插件模式 (Plugin Mode)** - 传统方式
   - PECJ 作为独立插件运行
   - 通过 PECJAdapter 接口调用
   - 独立线程和资源管理
   - 适用于快速原型和基准测试

2. **融合模式 (Integrated Mode)** - 推荐方式
   - PECJ 作为计算引擎深度集成
   - 数据存储在 sageTSDB 表中
   - 统一资源管理
   - 适用于生产环境

详细的双模式架构设计请参考：
- [DESIGN_DOC_SAGETSDB_PECJ.md](../DESIGN_DOC_SAGETSDB_PECJ.md) - 第3.1节

## 相关代码

- 实现代码：`src/plugins/`
  - `plugin_manager.cpp` - 插件管理器
  - `adapters/pecj_adapter.cpp` - PECJ 插件适配器
  - `adapters/fault_detection_adapter.cpp` - 故障检测插件

- 头文件：`include/sage_tsdb/plugins/`

- 测试代码：`tests/test_pecj_plugin.cpp`

## 使用示例

插件模式使用示例请参考：
- `examples/plugin_usage_example.cpp`

融合模式使用示例请参考：
- `examples/integrated_demo.cpp`
- `examples/pecj_replay_demo.cpp`

## 开发新插件

如需开发新的插件，请参考现有的 PECJAdapter 和 FaultDetectionAdapter 实现。

主要步骤：
1. 继承 `PluginBase` 基类
2. 实现 `initialize()`, `process()`, `getResults()` 等接口
3. 在 `PluginManager` 中注册插件
4. 编写单元测试
