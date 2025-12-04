# sageTSDB 示例程序

本目录包含 sageTSDB 的各种示例程序，帮助您快速了解和使用各项功能。

## 示例列表

### 1. 基础示例

#### persistence_example.cpp
**功能**: 数据持久化示例
- 演示如何保存和加载时序数据
- 检查点（Checkpoint）管理
- 增量写入和数据恢复

**运行方式**:
```bash
cd build/examples
./persistence_example
```

#### plugin_usage_example.cpp  
**功能**: 插件系统使用示例
- 插件加载和初始化
- 数据流处理
- 资源管理和监控

**运行方式**:
```bash
cd build/examples
./plugin_usage_example
```

### 2. PECJ 集成示例

#### integrated_demo.cpp
**功能**: sageTSDB 与 PECJ 集成演示
- 完整的集成模式演示
- 实时流处理
- 资源管理器集成

**运行方式**:
```bash
cd build/examples
./integrated_demo
```

#### pecj_replay_demo.cpp
**功能**: PECJ 数据回放演示
- 从数据集文件回放
- PECJ 算法应用
- 性能监控

**运行方式**:
```bash
cd build/examples
./pecj_replay_demo <数据集路径>
```

**详细文档**: [README_PECJ_DEMO.md](./README_PECJ_DEMO.md)

### 3. 性能测试

#### performance_benchmark.cpp
**功能**: 性能基准测试
- 吞吐量测试
- 延迟测试
- 资源使用分析

**运行方式**:
```bash
cd build/examples
./performance_benchmark
```

## 快速开始

### 构建示例

```bash
# 在 sageTSDB 根目录下
mkdir -p build
cd build
cmake ..
make

# 所有示例程序将在 build/examples/ 目录下
```

### 运行所有示例

使用提供的脚本一次性构建和运行所有示例：

```bash
cd examples
./build_and_test.sh
```

或者单独运行演示：

```bash
./run_demo.sh
```

## 示例配置

部分示例使用配置文件 `demo_configs.json` 进行参数设置。您可以修改此文件来调整：
- 数据路径
- 资源限制
- 算法参数
- 输出设置

## 数据文件

PECJ 相关示例需要数据集文件。默认数据路径配置在 `demo_configs.json` 中。
如果需要自定义数据，请参考 PECJ 数据格式文档。

## 疑难解答

### 编译错误
- 确保已安装所有依赖（见根目录 README.md）
- 检查 CMake 配置是否正确
- PECJ 集成示例需要 PECJ 库支持

### 运行错误
- 检查数据文件路径是否正确
- 确保有足够的磁盘空间（持久化示例）
- 查看日志输出获取详细错误信息

## 学习路径

如果您是第一次使用 sageTSDB，建议按以下顺序学习示例：

1. **persistence_example** - 了解基本的数据操作
2. **plugin_usage_example** - 理解插件系统
3. **integrated_demo** - 学习完整集成
4. **performance_benchmark** - 优化性能配置

## 更多信息

- **完整文档**: 参见 `docs/` 目录
- **单元测试**: 参见 `tests/` 目录作为 API 使用参考
- **快速入门**: [QUICKSTART.md](./QUICKSTART.md)
- **演示总结**: [DEMO_SUMMARY.md](./DEMO_SUMMARY.md)
