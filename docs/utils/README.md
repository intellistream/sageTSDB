# 工具模块文档

本目录包含 sageTSDB 工具和辅助功能的文档。

## 工具列表

### 配置管理 (Config)

**实现文件**：`src/utils/config.cpp`

**功能**：
- 系统配置参数管理
- 支持从文件加载配置
- 运行时配置更新
- 配置验证和默认值

**主要配置项**：
- 数据库路径和存储参数
- LSM Tree 配置（MemTable 大小、压缩策略）
- 线程池和资源限制
- PECJ 计算引擎参数
- 日志级别和输出配置

## 相关代码

- 实现代码：`src/utils/`
  - `config.cpp` - 配置管理

- 头文件：`include/sage_tsdb/utils/`
  - `config.h` - 配置接口
  - `logger.h` - 日志工具
  - `metrics.h` - 性能指标

## 配置示例

```cpp
#include <sage_tsdb/utils/config.h>

// 加载配置文件
Config config;
config.loadFromFile("config.json");

// 获取配置项
std::string db_path = config.get("database.path");
int memtable_size = config.getInt("storage.memtable_size");

// 运行时更新配置
config.set("thread_pool.size", 8);
```

## 日志系统

sageTSDB 使用分级日志系统：

- **ERROR**: 错误信息
- **WARN**: 警告信息
- **INFO**: 一般信息
- **DEBUG**: 调试信息
- **TRACE**: 详细追踪

配置日志级别：
```cpp
config.set("log.level", "INFO");
```

## 性能监控

工具模块提供性能指标收集：

- 操作延迟统计
- 吞吐量监控
- 资源使用跟踪
- 自定义指标

## 扩展工具

添加新的工具函数：

1. 在 `src/utils/` 中实现
2. 在 `include/sage_tsdb/utils/` 中声明接口
3. 编写单元测试
4. 更新本文档
