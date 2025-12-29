# run_and_visualize.sh 更新说明

## 更新内容

### 主要变更
`run_and_visualize.sh` 脚本已更新为使用新的 `visualize_timing.py` 来生成细粒度时间分析图表。

### 变更详情

#### 1. 可视化脚本切换
```bash
# 修改前
python3 visualize_benchmark.py "$JSON_FILE" --output "$PNG_FILE"

# 修改后
python3 visualize_timing.py
```

#### 2. 输出图表数量
```bash
# 修改前: 1个综合图表
PNG_FILE="benchmark_results_timestamp.png"

# 修改后: 5个专业分析图表
timing_comparison_bar.png       - 细粒度时间对比柱状图
timing_stacked_bar.png          - 时间分配堆叠图
timing_speedup.png              - 加速比分析图
timing_bottleneck_analysis.png  - 瓶颈贡献分解 ⭐
timing_summary_table.png        - 完整对比表格
```

#### 3. 默认参数调整
```bash
# 修改前
REPEAT=3

# 修改后
REPEAT=1  # 细粒度测量更准确，减少重复次数
```

#### 4. Benchmark 路径修正
```bash
# 修改前
BUILD_DIR="$SCRIPT_DIR/../build/examples"

# 修改后
BUILD_DIR="$SCRIPT_DIR/../build/benchmark"  # 正确的路径
```

## 使用方法

### 基本使用
```bash
cd /path/to/sageTSDB/examples
./run_and_visualize.sh
```

### 自定义参数
```bash
# 50K 事件，8 线程
./run_and_visualize.sh -e 50000 -t 8

# 自定义前缀，重复 3 次
./run_and_visualize.sh --prefix my_test --repeat 3

# 更大的窗口 (20ms window, 10ms slide)
./run_and_visualize.sh -w 20000 -s 10000
```

### 查看帮助
```bash
./run_and_visualize.sh --help
```

## 生成的文件

### 图表文件 (5个)
1. **timing_comparison_bar.png** (281KB)
   - 展示各阶段绝对时间对比
   - 使用对数刻度显示全部阶段
   - 适合展示整体性能差异

2. **timing_stacked_bar.png** (208KB)
   - 展示时间分配百分比
   - 直观对比资源使用结构
   - 突出显示主要开销来源

3. **timing_speedup.png** (245KB)
   - 左图: 绝对时间对比（对数刻度）
   - 右图: 各阶段加速比（颜色编码）
   - 适合展示性能提升幅度

4. **timing_bottleneck_analysis.png** (246KB) ⭐ **最重要**
   - 展示各优化点对总体性能的贡献
   - 定量分析: Data Access 贡献 59.4%
   - 适合论文核心论点展示

5. **timing_summary_table.png** (304KB)
   - 完整数据表格
   - 包含所有阶段的详细指标
   - 适合附录或补充材料

### 日志文件
```
benchmark_results_YYYYMMDD_HHMMSS.txt
```
包含完整的 benchmark 运行日志和详细时间数据。

## 关键发现

脚本运行完成后会显示关键发现：

```
关键发现:
  • 59.4% of speedup from Data Access optimization
  • 17.13× faster in Data Access (avoiding LSM-Tree I/O)
  • 1.87× overall speedup (Plugin vs Integrated)
```

这些数字来自实际的细粒度时间测量，可直接用于论文写作。

## 与旧版本对比

| 特性 | 旧版 (visualize_benchmark.py) | 新版 (visualize_timing.py) |
|------|-------------------------------|---------------------------|
| 图表数量 | 1个综合图 | 5个专业分析图 |
| 时间分解 | 仅 Total Time | 7个细粒度阶段 |
| 瓶颈分析 | 无 | 有（59.4% 贡献分析）⭐ |
| 论文适用性 | 中 | 高（英文标签） |
| 数据详细度 | 基础 | 详尽（包含表格） |
| 中文乱码 | 可能有 | 已修复（全英文） |

## 优势

### 1. 更详细的分析
- 7个时间阶段的细粒度测量
- 定量的瓶颈贡献分析
- 完整的数据表格

### 2. 论文友好
- 所有标签使用英文，无乱码
- 符合学术出版标准
- 5个图表可用于不同章节

### 3. 自动化流程
```bash
运行 benchmark → 生成 5 个图表 → 显示关键发现
```
一条命令完成全部工作！

### 4. 灵活配置
支持多种参数组合，适应不同实验需求。

## 示例输出

```bash
$ ./run_and_visualize.sh -e 20000 -t 4

==================================================
  PECJ Benchmark Fine-Grained Timing Analysis
==================================================
配置:
  事件数: 20000
  线程数: 4
  窗口长度: 10.00 ms
  滑动长度: 5.00 ms
  算子类型: IMA
  重复次数: 1

输出文件:
  文本日志: benchmark_results_20241229_193000.txt
  图表输出: timing_*.png (5个图表)
==================================================

[1/2] 运行 benchmark (细粒度时间测量)...
[Benchmark output...]

[2/2] 生成细粒度时间分析图表...
✓ Generated: timing_comparison_bar.png
✓ Generated: timing_stacked_bar.png
✓ Generated: timing_speedup.png
✓ Generated: timing_bottleneck_analysis.png
✓ Generated: timing_summary_table.png

==================================================
  完成! 结果文件:
==================================================
  📊 图表 (5个):
     1. timing_comparison_bar.png       - Fine-grained timing comparison
     2. timing_stacked_bar.png          - Time allocation breakdown
     3. timing_speedup.png              - Speedup analysis
     4. timing_bottleneck_analysis.png  - Bottleneck analysis ⭐
     5. timing_summary_table.png        - Complete comparison table

  📄 文本日志: benchmark_results_20241229_193000.txt
==================================================

关键发现:
  • 59.4% of speedup from Data Access optimization
  • 17.13× faster in Data Access (avoiding LSM-Tree I/O)
  • 1.87× overall speedup (Plugin vs Integrated)

查看图表:
  xdg-open timing_bottleneck_analysis.png  # 最重要!
  xdg-open timing_comparison_bar.png
```

## 故障排查

### 1. Benchmark 程序找不到
```bash
错误: 找不到 benchmark 程序
解决: cd ../build && cmake .. && make pecj_integrated_vs_plugin_benchmark
```

### 2. Python 依赖缺失
```bash
解决: pip install matplotlib numpy
```

### 3. 权限问题
```bash
解决: chmod +x run_and_visualize.sh
```

## 相关文档

- `FINE_GRAINED_TIMING_UPDATE.md` - Benchmark 代码修改说明
- `TIMING_ANALYSIS_REPORT.md` - 详细性能分析报告
- `VISUALIZATION_GUIDE.md` - 图表使用指南（论文写作）
- `FONT_FIX_NOTES.md` - 中文乱码修复说明

---

**更新日期**: 2024-12-29
**版本**: v2.0
**主要改进**: 切换到细粒度时间分析可视化
