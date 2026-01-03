# PECJ Benchmark 可视化快速入门

## 🚀 快速开始

### 方法 1: 一键运行（推荐）

```bash
cd examples
./run_and_visualize.sh
```

这将自动：
1. 运行 benchmark
2. 生成 JSON 数据
3. 创建可视化图表
4. 保存所有结果文件

### 方法 2: 手动运行

```bash
# 1. 运行 benchmark 并生成 JSON
cd build/examples
./pecj_integrated_vs_plugin_benchmark --json results.json

# 2. 生成可视化图表
cd ../../examples
python3 visualize_benchmark.py ../build/examples/results.json
```

## 📊 生成的文件

- `*.json` - Benchmark 原始数据（JSON 格式）
- `*.txt` - Benchmark 详细报告（文本格式）
- `*.png` - 性能对比可视化图表（高清图片）

## 🎨 可视化图表内容

生成的图表包含 6 个子图：

1. **总执行时间** - 两种模式的总时间对比
2. **时间分解** - Setup/Insert/Compute/Query/Cleanup 各阶段对比
3. **资源使用** - 内存/CPU/上下文切换对比（归一化）
4. **吞吐量** - Events/sec 和 Joins/sec 对比
5. **结果统计** - 事件数、窗口数、连接结果数对比
6. **配置信息** - Benchmark 参数显示

## ⚙️ 自定义参数

```bash
# 使用更多事件和线程
./run_and_visualize.sh --events 50000 --threads 8

# 调整窗口大小
./run_and_visualize.sh --window 20000 --slide 10000

# 自定义输出文件名
./run_and_visualize.sh --prefix my_test

# 查看所有选项
./run_and_visualize.sh --help
```

## 📖 详细文档

- [VISUALIZATION_GUIDE.md](VISUALIZATION_GUIDE.md) - 完整使用指南
- [README.md](README.md) - sageTSDB 示例文档

## 🔧 依赖安装

如果提示缺少依赖，运行：

```bash
pip install matplotlib numpy
```

## 💡 提示

- 图表以 300 DPI 保存，适合论文使用
- JSON 文件可用于进一步分析或自定义可视化
- 支持批量运行多个配置并对比

## 🐛 故障排除

### Q: 提示找不到 benchmark 程序
```bash
cd ../build
cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..
make pecj_integrated_vs_plugin_benchmark
```

### Q: Python 模块缺失
```bash
pip install matplotlib numpy
```

### Q: 图表显示字体警告
这是正常的，不影响图表生成。

---

**享受你的性能分析之旅！** 🎉
