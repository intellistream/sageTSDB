# sageTSDB 脚本工具

本目录包含用于构建、测试和运行 sageTSDB 示例的各种脚本工具。

---

## �� 目录结构

```
scripts/
├── README.md                  # 本文件 - 脚本工具总览
│
├── build/                     # 构建脚本
│   ├── README.md              # 构建脚本说明
│   ├── build.sh               # 主构建脚本
│   ├── build_and_test.sh      # 构建并测试
│   └── build_plugins.sh       # 插件系统构建
│
├── demo/                      # 演示脚本
│   ├── README.md              # 演示脚本说明
│   ├── run_demo.sh            # 交互式演示启动器
│   ├── run_disorder_demo.sh   # 乱序处理演示（合并自3个脚本）
│   └── run_pecj_shj_comparison.sh  # 算法对比演示
│
└── test/                      # 测试脚本
    ├── README.md              # 测试脚本说明
    ├── compare_pecj_modes.sh  # PECJ 模式对比
    └── test_lsm_tree.sh       # LSM Tree 测试
```

---

## 📚 脚本分类说明

### 🔨 [构建脚本](./build/)
用于编译项目和示例程序

| 脚本 | 功能 | 对应示例 | 运行时间 |
|------|------|---------|---------|
| **build.sh** | 主构建脚本 | 所有 examples/ | 2-5 分钟 |
| **build_and_test.sh** | 构建并测试 | basic/, integration/ | 5-10 分钟 |
| **build_plugins.sh** | 插件系统构建 | plugins/ | 2-3 分钟 |

👉 [查看构建脚本详细说明](./build/README.md)

---

### 🎮 [演示脚本](./demo/)
运行示例程序的预配置演示

| 脚本 | 功能 | 对应示例 | 运行时间 |
|------|------|---------|---------|
| **run_demo.sh** | 交互式演示启动器 | integration/, benchmarks/ | 5-30 分钟 |
| **run_disorder_demo.sh** | 乱序处理演示 | integration/deep_integration_demo | 5-30 分钟 |
| **run_pecj_shj_comparison.sh** | 算法对比演示 | integration/pecj_shj_comparison_demo | 10-15 分钟 |

👉 [查看演示脚本详细说明](./demo/README.md)

---

### 🧪 [测试脚本](./test/)
性能对比和功能测试

| 脚本 | 功能 | 对应测试 | 运行时间 |
|------|------|---------|---------|
| **compare_pecj_modes.sh** | PECJ 模式对比 | benchmarks/pecj_integrated_vs_plugin_benchmark | ~10 分钟 |
| **test_lsm_tree.sh** | LSM Tree 测试 | tests/test_storage_engine | ~5 分钟 |

👉 [查看测试脚本详细说明](./test/README.md)

---

## 🚀 快速开始指南

### 场景 1: 首次使用（5 分钟）

```bash
# 1. 构建项目
./scripts/build/build.sh

# 2. 运行基础演示
./scripts/demo/run_demo.sh
# 选择 "1) Basic Replay Demo"
```

---

### 场景 2: 性能评估（30 分钟）

```bash
# 1. 乱序处理能力测试
./scripts/demo/run_disorder_demo.sh benchmark

# 2. PECJ 模式对比
./scripts/test/compare_pecj_modes.sh

# 3. 可视化结果
cd examples/visualization
python3 visualize_timing.py
```

---

### 场景 3: 开发测试（10 分钟）

```bash
# 1. 构建并测试
./scripts/build/build_and_test.sh

# 2. 运行存储引擎测试
./scripts/test/test_lsm_tree.sh
```

---

### 场景 4: 功能演示（15 分钟）

```bash
# 1. 乱序处理快速演示
./scripts/demo/run_disorder_demo.sh quick

# 2. 算法对比演示
./scripts/demo/run_pecj_shj_comparison.sh
```

---

## 📖 脚本与示例对应关系

| 示例程序 | 对应脚本 | 目录 |
|---------|---------|------|
| **基础示例** |
| basic/persistence_example | build/build_and_test.sh | build/ |
| basic/table_design_demo | build/build_and_test.sh | build/ |
| basic/window_scheduler_demo | build/build.sh | build/ |
| **集成示例** |
| integration/pecj_replay_demo | demo/run_demo.sh | demo/ |
| integration/integrated_demo | demo/run_demo.sh | demo/ |
| integration/deep_integration_demo | demo/run_disorder_demo.sh | demo/ |
| integration/pecj_shj_comparison_demo | demo/run_pecj_shj_comparison.sh | demo/ |
| **性能测试** |
| benchmarks/performance_benchmark | demo/run_demo.sh | demo/ |
| benchmarks/pecj_integrated_vs_plugin_benchmark | test/compare_pecj_modes.sh | test/ |
| **插件系统** |
| plugins/plugin_usage_example | build/build_plugins.sh | build/ |

---

## 🔧 常见问题

### Q: 如何查看脚本的详细用法？

**A**: 每个脚本都支持 --help 参数，或直接查看对应目录的 README

### Q: 数据集文件在哪里？

**A**: 位于 examples/datasets/ 或 PECJ/benchmark/datasets/

### Q: 构建失败怎么办？

**A**: 清理 build/ 目录后重新运行 ./scripts/build/build.sh

---

## 📊 重构说明

**重构时间**: 2026-01-04  
**重构方案**: 按功能分类重组

**变更内容**:
- ✅ 创建 build/, demo/, test/ 三个功能目录
- ✅ 合并 3 个重复的乱序脚本为 demo/run_disorder_demo.sh
- ✅ 更新所有脚本路径以适配新的 examples/ 结构
- ✅ 删除过时的 CLEANUP_SUMMARY.md 文档
- ✅ 为每个子目录创建详细的 README 文档

---

## 📞 获取帮助

- 📖 查看子目录 README
- 💬 查看脚本中的详细注释
- 📚 查看示例程序文档 ../examples/README.md

---

**脚本工具使用愉快！** 🎉
