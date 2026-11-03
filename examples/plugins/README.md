# 插件系统示例

本目录包含 sageTSDB 插件系统的使用示例，展示如何扩展系统功能。

## 📚 示例列表

### 1. plugin_usage_example.cpp
**功能**: 插件系统完整使用演示

**演示内容**:
- 插件注册和加载
- 资源管理和配置
- 插件生命周期管理
- 插件间通信
- 错误处理和日志

**运行时间**: ~2 分钟

**运行方式**:
```bash
cd build/examples
./plugin_usage_example
```

---

## 🔌 插件类型说明

sageTSDB 支持以下类型的插件：

### 1. 计算引擎插件
- **功能**: 扩展数据处理能力
- **示例**: PECJ Join 引擎
- **接口**: `ComputeEnginePlugin`

### 2. 存储插件
- **功能**: 自定义存储后端
- **示例**: LSM-Tree、时序数据库
- **接口**: `StoragePlugin`

### 3. 分析插件
- **功能**: 数据分析和异常检测
- **示例**: 故障检测、预测分析
- **接口**: `AnalysisPlugin`

---

## 🛠️ 开发自定义插件

### 基本步骤

1. **继承插件基类**
```cpp
class MyPlugin : public sage::tsdb::ComputeEnginePlugin {
public:
    std::string getName() const override;
    void initialize(const PluginConfig& config) override;
    void execute() override;
    void shutdown() override;
};
```

2. **实现必要接口**
- `getName()`: 返回插件名称
- `initialize()`: 初始化插件
- `execute()`: 执行插件逻辑
- `shutdown()`: 清理资源

3. **注册插件**
```cpp
auto& manager = PluginManager::getInstance();
manager.registerPlugin(std::make_unique<MyPlugin>());
```

4. **配置插件**
```cpp
PluginConfig config;
config.set("param1", "value1");
manager.configurePlugin("MyPlugin", config);
```

---

## 📖 插件开发指南

### 最佳实践

1. **资源管理**: 在 `initialize()` 中分配，在 `shutdown()` 中释放
2. **错误处理**: 使用异常或返回错误码，确保不影响主系统
3. **线程安全**: 多线程环境下使用锁保护共享资源
4. **日志记录**: 使用统一的日志接口，便于调试
5. **配置验证**: 在 `initialize()` 中验证配置参数

### 性能优化

1. **避免阻塞**: 长时间操作应该异步执行
2. **内存效率**: 使用内存池，减少动态分配
3. **缓存友好**: 注意数据局部性
4. **批处理**: 批量处理数据以提高效率

---

## 🔍 测试插件

```bash
# 编译插件
cd build
cmake .. -DBUILD_PLUGINS=ON
make

# 运行测试
cd tests
./test_pecj_plugin
./test_fault_detection_plugin
```

---

## 📚 相关示例

- [PECJ 计算引擎插件](../../src/plugins/adapters/pecj_adapter.cpp)
- [故障检测分析插件](../../src/plugins/adapters/fault_detection_adapter.cpp)
- [插件单元测试](../../tests/test_pecj_plugin.cpp)

---

## 📖 相关文档

- [插件系统架构](../../docs/)
- [API 参考](../../docs/)
- [PECJ 集成示例](../integration/)
