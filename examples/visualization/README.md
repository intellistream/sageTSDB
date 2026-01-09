# 可视化工具

本目录包含用于分析和可视化 sageTSDB 性能测试结果的工具脚本。

## 🛠️ 工具列表

### 1. visualize_timing.py
**功能**: 细粒度时间分析可视化

生成 7 种图表，全面分析性能瓶颈：

#### 生成的图表

1. **timing_comparison_bar.png** - 时间阶段对比柱状图
   - 对比集成模式 vs 插件模式各阶段耗时

2. **timing_stacked_bar.png** - 时间占比堆叠图
   - 显示各阶段时间占总时间的百分比

3. **timing_speedup.png** - 加速比分析
   - 展示集成模式相比插件模式的加速效果

4. **timing_bottleneck_analysis.png** - 瓶颈分析
   - 识别性能瓶颈所在阶段

5. **timing_summary_table.png** - 性能汇总表
   - 详细的数值对比表格

6. **test_comprehensive.png** - 综合分析
   - 多维度性能对比

7. **test_results_visualization.png** - 结果可视化
   - 测试结果的综合展示

**使用方式**:
```bash
# 读取 JSON 结果并生成所有图表
python3 visualize_timing.py

# 图表输出到 ../outputs/figures/
```

**输入文件**: `../outputs/results/fine_grained_timing_results.json`

---

### 2. visualize_benchmark.py
**功能**: 通用性能测试结果可视化

**生成的图表**:
- 吞吐量对比
- 延迟分布
- 内存使用趋势
- CPU 利用率

**使用方式**:
```bash
python3 visualize_benchmark.py \
    ../outputs/results/benchmark_results.json \
    --output ../outputs/figures/benchmark_analysis.png
```

---

### 3. run_and_visualize.sh
**功能**: 一键运行测试并生成可视化

**执行流程**:
1. 编译最新代码
2. 运行 benchmark
3. 生成可视化图表
4. 输出结果路径

**使用方式**:
```bash
# 运行完整流程
./run_and_visualize.sh

# 查看结果
ls -lh ../outputs/figures/
```

**环境要求**:
- Python 3.6+
- matplotlib
- numpy
- pandas (可选)

---

### 4. test_fine_grained_timing.sh
**功能**: 细粒度时间测试专用脚本

**执行流程**:
1. 运行 `pecj_integrated_vs_plugin_benchmark`
2. 收集细粒度时间数据
3. 调用 `visualize_timing.py` 生成图表

**使用方式**:
```bash
./test_fine_grained_timing.sh
```

---

## 📊 可视化快速开始

### 场景 1: 查看已有结果
```bash
cd visualization
python3 visualize_timing.py
# 查看 ../outputs/figures/ 中的图表
```

### 场景 2: 运行新测试并可视化
```bash
cd visualization
./run_and_visualize.sh
```

### 场景 3: 自定义可视化
```bash
python3 visualize_benchmark.py \
    ../outputs/results/my_test.json \
    --output ../outputs/figures/my_analysis.png \
    --title "Custom Analysis"
```

---

## 🔧 依赖安装

```bash
# Ubuntu/Debian
sudo apt-get install python3-pip
pip3 install matplotlib numpy

# macOS
brew install python3
pip3 install matplotlib numpy

# 验证安装
python3 -c "import matplotlib; print('OK')"
```

---

## 📈 图表说明

### 时间阶段定义

1. **Setup Time**: 系统初始化、配置加载
2. **Data Preparation**: 数据排序、预处理
3. **Data Access**: 数据读取（DB查询 vs 内存访问）
4. **Pure Compute**: 纯 Join 计算时间
5. **Result Writing**: 结果写入存储

### 性能指标

- **总时间**: 端到端执行时间
- **加速比**: Speedup = T_plugin / T_integrated
- **时间占比**: 各阶段占总时间的百分比
- **瓶颈识别**: 耗时最长的阶段

---

## 🎨 自定义图表样式

编辑 `visualize_timing.py`:

```python
# 修改颜色方案
COLORS = {
    'integrated': '#2E86AB',  # 蓝色
    'plugin': '#A23B72',      # 紫色
}

# 修改图表尺寸
plt.figure(figsize=(12, 6))

# 修改字体
plt.rcParams['font.size'] = 12
```

---

## 📖 相关文档

- [性能测试说明](../benchmarks/README.md)
- [PECJ Benchmark 文档](../../docs/compute/PECJ_BENCHMARK_README.md)
- [测试结果分析](../outputs/results/)
