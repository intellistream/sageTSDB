# sageTSDB 示例程序

本目录包含 sageTSDB 的各种示例程序，展示核心功能和 PECJ 集成能力。

> 📖 **详细文档**: 查看 [docs/examples/](../docs/examples/) 获取完整的使用指南和配置说明。

## 🚀 快速开始

### 构建示例程序

```bash
# 进入项目根目录
cd /path/to/sageTSDB

# 创建并进入 build 目录
mkdir -p build && cd build

# 配置 CMake (启用 PECJ 集成)
cmake .. \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR=/path/to/PECJ

# 编译所有示例
make -j$(nproc)

# 示例程序位于
ls build/examples/
```

### 运行第一个示例

```bash
cd build/examples

# 基础功能演示
./persistence_example

# PECJ 流式 Join 演示
./pecj_replay_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 5000
```

---

## 📚 示例程序列表

### 基础功能示例

| 示例程序 | 功能说明 | 运行时间 | 文档 |
|---------|---------|---------|------|
| **persistence_example** | 数据持久化、检查点管理 | ~2 分钟 | [代码](./persistence_example.cpp) |
| **plugin_usage_example** | 插件系统使用、资源管理 | ~2 分钟 | [代码](./plugin_usage_example.cpp) |
| **table_design_demo** | 表设计、数据插入查询 | ~1 分钟 | [代码](./table_design_demo.cpp) |
| **window_scheduler_demo** | 窗口调度、触发机制 | ~2 分钟 | [代码](./window_scheduler_demo.cpp) |

### PECJ 集成示例

| 示例程序 | 功能说明 | 运行时间 | 文档 |
|---------|---------|---------|------|
| **pecj_replay_demo** | 基础流式 Join，数据重放 | ~5 分钟 | [代码](./pecj_replay_demo.cpp) |
| **integrated_demo** | PECJ + 故障检测端到端演示 | ~10 分钟 | [代码](./integrated_demo.cpp) |
| **performance_benchmark** | 多维度性能评估对比 | 15-30 分钟 | [代码](./performance_benchmark.cpp) |
| **deep_integration_demo** | 深度集成架构、乱序处理 | ~15 分钟 | [代码](./deep_integration_demo.cpp) · [文档](../docs/examples/README_DEEP_INTEGRATION_DEMO.md) |

---

## 🎯 使用场景指南

### 场景 1: 学习基础功能
**适合**: 新用户、功能学习

```bash
# 1. 数据持久化
./persistence_example

# 2. 表操作
./table_design_demo

# 3. 窗口调度
./window_scheduler_demo
```

### 场景 2: 快速演示（5分钟）
**适合**: 快速展示系统能力

```bash
./pecj_replay_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 5000 \
    --operator IMA
```

### 场景 3: 完整功能演示（10分钟）
**适合**: 展示端到端数据处理管道

```bash
./integrated_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-tuples 10000 \
    --detection zscore \
    --threshold 3.0
```

### 场景 4: 性能评估（15分钟）
**适合**: 技术评估、性能对比

```bash
# 运行预定义测试套件
../scripts/run_high_disorder_demo.sh all

# 或自定义参数
./deep_integration_demo \
    --s-file ../../../PECJ/benchmark/datasets/sTuple.csv \
    --r-file ../../../PECJ/benchmark/datasets/rTuple.csv \
    --max-s 200000 \
    --max-r 200000 \
    --disorder-ratio 0.3
```

参考: [高乱序演示文档](../docs/examples/README_HIGH_DISORDER_DEMO.md)

---

## 📖 详细文档

完整的使用指南、配置说明和最佳实践，请参阅:

- **[示例程序索引](../docs/examples/README.md)** - 所有示例的详细文档入口
- **[Deep Integration Demo](../docs/examples/README_DEEP_INTEGRATION_DEMO.md)** - 深度集成架构详解
- **[High Disorder Demo](../docs/examples/README_HIGH_DISORDER_DEMO.md)** - 高乱序场景测试指南

其他相关文档:
- [sageTSDB 设计文档](../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [PECJ 计算引擎实现](../docs/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)
- [资源管理器指南](../docs/RESOURCE_MANAGER_GUIDE.md)

---

## 🔧 常见问题

### Q: 编译时找不到 PECJ
**A**: 确保正确设置 CMake 参数:
```bash
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_DIR=/path/to/PECJ
```

### Q: 运行时提示找不到数据文件
**A**: 使用绝对路径或从 `build/examples/` 目录运行:
```bash
cd build/examples
./pecj_replay_demo --s-file <绝对路径>/sTuple.csv ...
```

### Q: 如何选择合适的 PECJ 算子？
**A**: 
- **IMA**: 增量维护聚合，适合大部分场景（推荐）
- **SHJ**: 对称哈希 Join，适合均匀分布数据
- **MSWJ**: 多路分段窗口 Join，适合高并发

### Q: 如何调整性能？
**A**: 主要参数:
- `--threads`: 线程数（建议设为 CPU 核心数）
- `--window-ms`: 窗口大小（根据数据特征调整）
- `--max-tuples`: 处理数据量（影响总运行时间）

---

## 📊 数据集说明

### 标准数据集位置
```
PECJ/benchmark/datasets/
├── sTuple.csv          # S 流数据 (~60K 条)
├── rTuple.csv          # R 流数据 (~77K 条)
└── stock/              # 股票交易数据
    └── ...
```

### CSV 格式
```csv
key,value,eventTime,arrivalTime
51209364,1,0,455000
86971226,1,0,455000
```

- **key**: Join 键
- **value**: 元组值
- **eventTime**: 事件时间（微秒）
- **arrivalTime**: 到达时间（微秒）

---

## 🤝 贡献

如果你创建了新的示例程序:

1. 添加源文件到 `examples/` 目录
2. 更新 `examples/CMakeLists.txt`
3. 在本 README 中添加简要说明
4. 如需详细文档，在 `docs/examples/` 中创建文档文件
5. 提交 Pull Request

---

## 📞 获取帮助

- 查看 [完整文档](../docs/examples/)
- 查看源代码中的详细注释
- 运行示例时使用 `--help` 参数查看所有选项

---

**下一步**: 选择一个示例程序开始探索，或查看 [示例文档索引](../docs/examples/README.md) 了解更多！
