# Scripts 文件夹整理总结

**日期**: 2025-12-29  
**整理内容**: 删除过时/重复脚本，更新文档，标注脚本与示例的对应关系

---

## 🗑️ 已删除的脚本（过时/重复）

以下脚本功能已被其他脚本覆盖，已删除：

1. ❌ **build_pecj_integrated.sh** 
   - 原因：功能已被 `build.sh` 完全覆盖
   - 替代：使用 `build.sh`（支持 PECJ 深度集成模式）

2. ❌ **build_table_design.sh**
   - 原因：功能已被 `build.sh` 和 `build_and_test.sh` 覆盖
   - 替代：使用 `build.sh` 构建，`build_and_test.sh` 运行测试

3. ❌ **build_window_scheduler.sh**
   - 原因：功能已被 `build.sh` 覆盖
   - 替代：使用 `build.sh`（自动构建所有组件）

---

## ✅ 保留的脚本（9个）

### 构建脚本（3个）
1. **build.sh** - 主构建脚本
   - 对应示例：所有 examples/
2. **build_and_test.sh** - 构建并测试
   - 对应示例：persistence_example.cpp, table_design_demo.cpp, pecj_replay_demo.cpp, integrated_demo.cpp
3. **build_plugins.sh** - 插件系统构建
   - 对应示例：plugin_usage_example.cpp

### 演示运行脚本（4个）
4. **run_demo.sh** - 交互式演示启动器
   - 对应示例：pecj_replay_demo.cpp, integrated_demo.cpp, performance_benchmark.cpp
5. **run_high_disorder_demo.sh** - 高乱序场景测试
   - 对应示例：deep_integration_demo.cpp
6. **demo_disorder_showcase.sh** - 乱序能力展示
   - 对应示例：deep_integration_demo.cpp
7. **benchmark_disorder.sh** - 乱序性能基准
   - 对应示例：deep_integration_demo.cpp

### 测试和对比脚本（2个）
8. **compare_pecj_modes.sh** - PECJ 模式对比
   - 对应示例：pecj_integrated_vs_plugin_benchmark.cpp
9. **test_lsm_tree.sh** - LSM Tree 测试
   - 对应测试：test_storage_engine.cpp

---

## 📋 脚本与示例的完整对应关系

| 示例程序 | 对应脚本 | 说明 |
|---------|---------|------|
| `persistence_example.cpp` | `build_and_test.sh` | 持久化测试 |
| `plugin_usage_example.cpp` | `build_plugins.sh` | 插件系统演示 |
| `table_design_demo.cpp` | `build_and_test.sh` | 表设计测试 |
| `window_scheduler_demo.cpp` | `build.sh` | 窗口调度演示 |
| `pecj_replay_demo.cpp` | `run_demo.sh` | 基础流 Join |
| `integrated_demo.cpp` | `run_demo.sh` | 端到端集成 |
| `performance_benchmark.cpp` | `run_demo.sh` | 性能基准测试 |
| `deep_integration_demo.cpp` | `run_high_disorder_demo.sh`<br>`demo_disorder_showcase.sh`<br>`benchmark_disorder.sh` | 乱序处理演示 |
| `pecj_integrated_vs_plugin_benchmark.cpp` | `compare_pecj_modes.sh` | 模式对比 |
| `pecj_shj_comparison_demo.cpp` | `build.sh` | 算子对比 |

---

## 📖 文档更新

### scripts/README.md 重写内容

新的 README.md 包含：

1. **清晰的分类索引**
   - 构建脚本（3个）
   - 演示运行脚本（4个）
   - 测试和对比脚本（2个）

2. **每个脚本的详细说明**
   - 功能描述
   - 对应的 examples 文件（🎯 主要对应，✅ 相关对应）
   - 使用方法和参数
   - 输出说明
   - 适用场景

3. **使用指南**
   - 脚本使用流程图
   - 4种快速开始场景
   - 示例程序与脚本对应关系总览表

4. **常见问题解答**
   - 如何查看脚本用法
   - PECJ 数据集位置
   - 构建失败排查
   - 并行测试方法
   - 清理构建产物

5. **贡献指南**
   - 脚本命名规范
   - 文档更新要求
   - 脚本模板

---

## 🎯 整理效果

### 之前（12个脚本）
```
benchmark_disorder.sh
build_and_test.sh
build_pecj_integrated.sh       ❌ 已删除（重复）
build_plugins.sh
build_table_design.sh          ❌ 已删除（重复）
build_window_scheduler.sh      ❌ 已删除（重复）
build.sh
compare_pecj_modes.sh
demo_disorder_showcase.sh
README.md
run_demo.sh
run_high_disorder_demo.sh
test_lsm_tree.sh
```

### 之后（9个脚本 + 更新的文档）
```
benchmark_disorder.sh          ✅ 保留 (deep_integration_demo.cpp)
build_and_test.sh              ✅ 保留 (多个示例测试)
build_plugins.sh               ✅ 保留 (plugin_usage_example.cpp)
build.sh                       ✅ 保留 (所有示例)
compare_pecj_modes.sh          ✅ 保留 (pecj_integrated_vs_plugin_benchmark.cpp)
demo_disorder_showcase.sh      ✅ 保留 (deep_integration_demo.cpp)
README.md                      ✅ 重写 (清晰标注对应关系)
run_demo.sh                    ✅ 保留 (pecj_replay_demo.cpp 等)
run_high_disorder_demo.sh      ✅ 保留 (deep_integration_demo.cpp)
test_lsm_tree.sh               ✅ 保留 (test_storage_engine.cpp)
```

---

## 📊 改进点

1. **消除冗余**: 删除3个功能重复的脚本，减少25%的脚本数量
2. **清晰映射**: 每个脚本都明确标注了对应的 examples 文件
3. **分类组织**: 按功能分为构建、演示、测试三大类
4. **文档完善**: 重写 README，包含详细用法、场景指南和对应关系表
5. **易于维护**: 提供脚本模板和贡献指南，方便后续扩展

---

## 🚀 后续使用建议

### 首次使用
```bash
./scripts/build.sh
./scripts/run_demo.sh
```

### 性能评估
```bash
./scripts/run_high_disorder_demo.sh all
./scripts/benchmark_disorder.sh
```

### 功能演示
```bash
./scripts/demo_disorder_showcase.sh
```

### 开发测试
```bash
./scripts/build_and_test.sh
./scripts/test_lsm_tree.sh
```

---

**整理完成！** 🎉

查看完整文档：`scripts/README.md`
