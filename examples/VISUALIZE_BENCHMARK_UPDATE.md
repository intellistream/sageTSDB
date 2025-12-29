# visualize_benchmark.py 更新说明

## 概述

`visualize_benchmark.py` 已更新为使用细粒度时间数据，生成**单个综合性图表**，包含4个子图，全面展示PECJ Benchmark性能分析。

## 更新内容

### 主要变更

1. **数据源切换**
   ```python
   # 修改前: 从 JSON 文件读取
   data = load_benchmark_data(json_file)
   
   # 修改后: 使用硬编码的细粒度时间数据
   INTEGRATED_MODE = {'Total Time': 2444.89, 'Data Access': 718.21, ...}
   PLUGIN_MODE = {'Total Time': 1306.91, 'Data Access': 41.93, ...}
   ```

2. **图表布局**
   ```
   修改前: 3×3 网格，6个子图（总时间、时间分解、资源、吞吐量、结果、配置信息）
   修改后: 2×2 网格，4个核心子图（时间分解、总时间、瓶颈分析、加速比）
   ```

3. **新增功能**
   - 细粒度时间分解（7个阶段）
   - 瓶颈贡献分析（59.4% I/O 优化）
   - 加速比热力图（颜色编码）
   - 关键发现文本框

### 生成的图表结构

```
┌─────────────────────────────────────────────────────────┐
│  PECJ Benchmark: Integrated vs Plugin Mode             │
│  (20,000 events, 387 windows, 13,856 joins, 4 threads) │
├──────────────────────────┬──────────────────────────────┤
│  1. Fine-Grained Timing  │  2. Total Execution Time    │
│     Breakdown            │     (with 1.87× speedup)    │
│  (7个阶段对比，对数刻度)  │  (两个柱子 + 加速标注)       │
├──────────────────────────┼──────────────────────────────┤
│  3. Performance          │  4. Plugin Mode Speedup      │
│     Improvement Sources  │     by Phase                 │
│  (瓶颈贡献分析 ⭐)        │  (各阶段加速比热力图)        │
└──────────────────────────┴──────────────────────────────┘
       Key Findings (底部文本框)
```

## 使用方法

### 基本使用
```bash
cd /path/to/sageTSDB/examples
python3 visualize_benchmark.py
```

输出: `comprehensive_benchmark_analysis.png` (638KB)

### 自定义输出
```bash
python3 visualize_benchmark.py --output my_analysis.png
python3 visualize_benchmark.py -o results/benchmark.png
```

### 查看帮助
```bash
python3 visualize_benchmark.py --help
```

## 输出示例

```
======================================================================
  PECJ Benchmark Comprehensive Visualization
======================================================================

Generating comprehensive analysis chart...
Output: comprehensive_benchmark_analysis.png

✓ Generated: comprehensive_benchmark_analysis.png

======================================================================
  Summary
======================================================================
Total Time:     Integrated=2444.89ms  Plugin=1306.91ms
Speedup:        1.87×
Data Access:    17.13× faster
Pure Compute:   1.37× faster
======================================================================
```

## 图表内容详解

### 1. 左上角：Fine-Grained Timing Breakdown
- **内容**: 5个关键阶段的时间对比
- **刻度**: 对数刻度（显示从0.05ms到1675ms的数据）
- **阶段**:
  - Data Preparation (数据准备)
  - Data Access (数据访问) ⭐
  - Pure Compute (纯计算)
  - Result Writing (结果写入)
  - Setup Time (初始化)
- **用途**: 直观对比各阶段绝对时间

### 2. 右上角：Total Execution Time
- **内容**: 总执行时间柱状图
- **标注**: 
  - Integrated: 2444.89 ms
  - Plugin: 1306.91 ms
  - 加速比: 1.87×（橙色标注框）
- **用途**: 展示整体性能差异

### 3. 左下角：Performance Improvement Sources ⭐ **最重要**
- **内容**: 瓶颈贡献分解（横向条形图）
- **关键数据**:
  - Data Access Optimization: +59.4% (绿色)
  - Pure Compute Optimization: +40.0% (绿色)
  - Data Prep Overhead: -0.9% (红色)
  - Setup Overhead: -0.04% (红色)
- **用途**: 定量展示性能提升来源

### 4. 右下角：Plugin Mode Speedup by Phase
- **内容**: 各阶段加速比（横向条形图）
- **颜色编码**:
  - 绿色: >10× 加速（Data Access: 17.13×）
  - 橙色: 2-10× 加速
  - 蓝色: 1-2× 加速（Pure Compute: 1.37×）
  - 红色: <1× (变慢)
- **用途**: 突出显示关键性能提升点

### 5. 底部：Key Findings 文本框
```
Key Findings:
• 59.4% of speedup from Data Access optimization
• 17.13× faster in Data Access (avoiding LSM-Tree I/O)
• 1.87× overall speedup (Plugin vs Integrated)
• Pure Compute: 1.37× faster with optimized algorithm
```

## 与 visualize_timing.py 对比

| 特性 | visualize_benchmark.py | visualize_timing.py |
|------|----------------------|-------------------|
| 输出数量 | **1个综合图表** | 5个专业图表 |
| 文件大小 | 638KB | 总计 ~1.3MB |
| 布局 | 2×2 网格 | 独立图表 |
| 适用场景 | PPT演示、报告摘要 | 论文正文、详细分析 |
| 信息密度 | 高（4个子图） | 中（每图1个主题） |
| 打印友好 | 优秀（单页） | 良好（多页） |

## 使用建议

### 适合场景

1. **演讲/演示**
   - PPT只需一张图即可展示全部关键信息
   - 4个子图布局清晰，易于讲解

2. **报告摘要**
   - 执行摘要需要综合性图表
   - 一张图概括所有发现

3. **快速对比**
   - 无需查看多个文件
   - 所有关键指标集中展示

### 不适合场景

1. **论文正文** → 使用 `visualize_timing.py` 的独立图表
2. **详细分析** → 使用 5个专业图表
3. **打印多图** → 独立图表更清晰

## 技术细节

### 数据来源
```python
# 实际 benchmark 测试数据（20K events, 387 windows）
INTEGRATED_MODE = {
    'Total Time': 2444.89,      # ms
    'Data Access': 718.21,       # ms (29.4% of total)
    'Pure Compute': 1675.83,     # ms (68.5% of total)
    # ... 其他阶段
}
```

### 关键计算
```python
# 总体加速比
speedup = 2444.89 / 1306.91 = 1.87×

# Data Access 加速比
data_access_speedup = 718.21 / 41.93 = 17.13×

# 瓶颈贡献
data_access_contribution = (718.21 - 41.93) / (2444.89 - 1306.91) = 59.4%
```

### 颜色方案
```python
colors = {
    'integrated': '#E74C3C',  # 红色
    'plugin': '#3498DB',      # 蓝色
    'neutral': '#F39C12',     # 橙色
    'positive': '#27AE60',    # 绿色（正贡献）
    'negative': '#E74C3C'     # 红色（负贡献）
}
```

## 常见问题

### Q: 为什么不从JSON读取数据？
**A**: 细粒度时间数据来自benchmark直接输出，硬编码确保数据一致性和脚本独立性。如需更新数据，直接修改脚本中的字典即可。

### Q: 能否添加更多子图？
**A**: 可以，修改 `gs = fig.add_gridspec(2, 2, ...)` 为更大的网格，并添加新的绘图函数。

### Q: 如何修改图表大小？
**A**: 修改 `fig = plt.figure(figsize=(18, 12))` 中的 (18, 12) 参数。

### Q: 能否导出为PDF？
**A**: 可以，使用 `--output analysis.pdf` 即可。Matplotlib支持多种格式（PNG, PDF, SVG, EPS）。

## 文件信息

- **文件名**: `visualize_benchmark.py`
- **大小**: ~284 行代码
- **依赖**: matplotlib, numpy
- **输出**: 高分辨率PNG图表（300 DPI）
- **图表尺寸**: 18×12 英寸（4879×3516 像素）

## 更新历史

- **v2.0** (2024-12-29): 切换到细粒度时间数据，新增瓶颈分析
- **v1.0** (2024-12-15): 初始版本，从JSON读取数据

---

**建议**: 
- 需要综合性单图 → 使用 `visualize_benchmark.py`
- 需要专业多图 → 使用 `visualize_timing.py`
- 两者可以同时使用！
