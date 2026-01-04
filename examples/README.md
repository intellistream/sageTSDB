# sageTSDB 示例程序

本目录包含 sageTSDB 的各种示例程序，展示核心功能和 PECJ 集成能力。

> 📖 **详细文档**: 查看 [docs/examples/](../docs/examples/) 获取完整的使用指南和配置说明。

## 📁 目录结构

```
examples/
├── README.md                      # 本文件 - 示例程序总览
├── CMakeLists.txt                 # 构建配置
├── .gitignore                     # 忽略输出文件
│
├── basic/                         # 基础功能示例
│   ├── README.md                  # 基础示例说明
│   ├── persistence_example.cpp
│   ├── table_design_demo.cpp
│   └── window_scheduler_demo.cpp
│
├── integration/                   # PECJ 集成示例
│   ├── README.md                  # 集成示例说明
│   ├── pecj_replay_demo.cpp
│   ├── pecj_shj_comparison_demo.cpp
│   ├── integrated_demo.cpp
│   └── deep_integration_demo.cpp
│
├── benchmarks/                    # 性能测试
│   ├── README.md                  # 性能测试说明
│   ├── performance_benchmark.cpp
│   ├── pecj_integrated_vs_plugin_benchmark.cpp
│   └── configs/
│       └── demo_configs.json
│
├── plugins/                       # 插件系统示例
│   ├── README.md                  # 插件开发指南
│   └── plugin_usage_example.cpp
│
├── visualization/                 # 可视化工具
│   ├── README.md                  # 可视化工具说明
│   ├── visualize_timing.py
│   ├── visualize_benchmark.py
│   ├── run_and_visualize.sh
│   └── test_fine_grained_timing.sh
│
├── datasets/                      # 测试数据集
│   ├── README.md                  # 数据集说明
│   ├── sTuple.csv
│   └── rTuple.csv
│
└── outputs/                       # 运行结果（.gitignore）
    ├── results/                   # JSON/TXT 结果
    └── figures/                   # PNG 图表
```

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
./basic/persistence_example

# PECJ 流式 Join 演示
./integration/pecj_replay_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --max-tuples 5000
```

---

## 📚 示例程序分类

### 🔰 [基础功能示例](./basic/)
适合新用户学习 sageTSDB 核心概念

| 示例程序 | 功能说明 | 运行时间 |
|---------|---------|---------|
| **persistence_example** | 数据持久化、检查点管理 | ~2 分钟 |
| **table_design_demo** | 表设计、数据插入查询 | ~1 分钟 |
| **window_scheduler_demo** | 窗口调度、触发机制 | ~2 分钟 |

👉 [查看基础示例详细说明](./basic/README.md)

---

### 🔗 [PECJ 集成示例](./integration/)
展示 sageTSDB 与 PECJ 流式 Join 引擎的集成

| 示例程序 | 功能说明 | 运行时间 |
|---------|---------|---------|
| **pecj_replay_demo** | 基础流式 Join，数据重放 | ~5 分钟 |
| **pecj_shj_comparison_demo** | PECJ vs SHJ 算法对比 | ~8 分钟 |
| **integrated_demo** | PECJ + 故障检测端到端演示 | ~10 分钟 |
| **deep_integration_demo** | 深度集成架构、乱序处理 | ~15 分钟 |

👉 [查看集成示例详细说明](./integration/README.md)

---

### 📊 [性能测试](./benchmarks/)
系统性能评估和瓶颈分析

| 测试程序 | 功能说明 | 运行时间 |
|---------|---------|---------|
| **performance_benchmark** | 多维度性能评估对比 | 15-30 分钟 |
| **pecj_integrated_vs_plugin_benchmark** | 集成模式 vs 插件模式对比 | ~10 分钟 |

👉 [查看性能测试详细说明](./benchmarks/README.md)

---

### 🔌 [插件系统示例](./plugins/)
学习如何扩展 sageTSDB 功能

| 示例程序 | 功能说明 | 运行时间 |
|---------|---------|---------|
| **plugin_usage_example** | 插件系统使用、资源管理 | ~2 分钟 |

👉 [查看插件开发指南](./plugins/README.md)

---

### 📈 [可视化工具](./visualization/)
性能分析和结果可视化

| 工具 | 功能说明 |
|------|---------|
| **visualize_timing.py** | 细粒度时间分析，生成 7 种图表 |
| **visualize_benchmark.py** | 通用性能测试结果可视化 |
| **run_and_visualize.sh** | 一键运行测试并生成图表 |

👉 [查看可视化工具说明](./visualization/README.md)

---

### 📦 [测试数据集](./datasets/)
示例程序使用的测试数据

| 数据集 | 说明 | 规模 |
|--------|------|------|
| **sTuple.csv** | S 流数据（左表） | ~50K 条 |
| **rTuple.csv** | R 流数据（右表） | ~50K 条 |

👉 [查看数据集详细说明](./datasets/README.md)

---

## 🎯 使用场景指南

### 场景 1: 学习基础功能（推荐新用户）
**适合**: 首次使用 sageTSDB 的用户

```bash
cd build/examples

# 1. 数据持久化
./basic/persistence_example

# 2. 表操作
./basic/table_design_demo

# 3. 窗口调度
./basic/window_scheduler_demo
```

👉 更多信息: [基础示例](./basic/README.md)

---

### 场景 2: 快速演示（5 分钟）
**适合**: 快速展示系统能力

```bash
cd build/examples
./integration/pecj_replay_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --max-tuples 5000 \
    --operator IMA
```

👉 更多信息: [集成示例](./integration/README.md)

---

### 场景 3: 完整功能演示（10 分钟）
**适合**: 展示端到端数据处理管道

```bash
cd build/examples
./integration/integrated_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --max-tuples 10000
```

---

### 场景 4: 性能评估（15-30 分钟）
**适合**: 技术评估、性能对比

```bash
cd build/examples

# 运行性能测试
./benchmarks/performance_benchmark \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv \
    --output-json ../../examples/outputs/results/benchmark.json

# 生成可视化图表
cd ../../examples/visualization
python3 visualize_benchmark.py \
    ../outputs/results/benchmark.json
```

👉 更多信息: [性能测试](./benchmarks/README.md) | [可视化工具](./visualization/README.md)

---

## 📖 详细文档

### 子目录文档
- 📁 [基础功能示例](./basic/README.md)
- 📁 [PECJ 集成示例](./integration/README.md)
- 📁 [性能测试](./benchmarks/README.md)
- 📁 [插件系统](./plugins/README.md)
- 📁 [可视化工具](./visualization/README.md)
- 📁 [测试数据集](./datasets/README.md)

### 项目文档
- [sageTSDB 快速入门](../QUICKSTART.md)
- [sageTSDB 设计文档](../docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [PECJ 计算引擎实现](../docs/compute/)
- [资源管理器指南](../docs/)

---

## 🔧 常见问题

### Q: 编译时找不到 PECJ
**A**: 确保正确设置 CMake 参数:
```bash
cmake .. -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_DIR=/path/to/PECJ
```

### Q: 运行时提示找不到数据文件
**A**: 使用相对于 build/examples 的路径:
```bash
cd build/examples
./integration/pecj_replay_demo \
    --s-file ../../examples/datasets/sTuple.csv \
    --r-file ../../examples/datasets/rTuple.csv
```

或使用绝对路径。

### Q: 如何选择合适的 PECJ 算子？
**A**: 
- **IMA**: 增量维护聚合，适合大部分场景（推荐）
- **SHJ**: 对称哈希 Join，适合均匀分布数据
- **MWAY**: 多路 Join，适合多表场景
- **PMJAM**: 分区多路 Join，适合大规模数据

详见: [集成示例文档](./integration/README.md)

### Q: 如何查看生成的图表？
**A**: 
```bash
# 运行可视化脚本后
cd examples/outputs/figures
ls -lh *.png
# 使用图片查看器打开
```

### Q: 可以自定义数据集吗？
**A**: 可以！参考 [数据集说明](./datasets/README.md) 了解 CSV 格式要求。

---

## 🤝 贡献

如果你创建了新的示例程序：

1. 将源文件添加到相应的功能目录（basic/integration/benchmarks/plugins）
2. 更新对应子目录的 README.md
3. 更新 `examples/CMakeLists.txt` 添加编译目标
4. 在本 README 的对应分类中添加简要说明
5. 提交 Pull Request

---

## 📞 获取帮助

- 📖 查看 [子目录文档](#详细文档) 获取详细说明
- 💬 查看源代码中的详细注释
- ❓ 运行示例时使用 `--help` 参数查看所有选项
- 🐛 遇到问题？提交 [Issue](https://github.com/intellistream/PECJ/issues)

---

**下一步**: 
- 🔰 新用户？从 [基础示例](./basic/) 开始
- 🚀 想快速上手？运行 [集成示例](./integration/)
- 📊 需要评估性能？查看 [性能测试](./benchmarks/)

**祝使用愉快！** 🎉
