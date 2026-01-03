# Font Fix Notes - 图表乱码问题修复

## 问题描述
原始的 `visualize_timing.py` 脚本使用中文标签，但系统缺少中文字体，导致图表中的标题、坐标轴标签显示为方框乱码。

## 解决方案
将所有图表标签从中文改为英文，避免字体依赖问题。

## 修改内容

### 1. timing_comparison_bar.png
```
修改前: 时间阶段 (X轴), 时间 (毫秒) (Y轴), PECJ Benchmark 细粒度时间对比 (标题)
修改后: Timing Phase (X轴), Time (milliseconds) (Y轴), PECJ Benchmark Fine-Grained Timing Comparison (标题)
```

### 2. timing_stacked_bar.png
```
修改前: 时间占比 (%) (Y轴), PECJ 时间分配对比 (标题)
修改后: Time Allocation (%) (Y轴), PECJ Time Allocation Breakdown (标题)
```

### 3. timing_speedup.png
```
修改前: 
  - 时间 (毫秒, 对数刻度) (左图Y轴)
  - 绝对时间对比 (左图标题)
  - 加速比 (Integrated / Plugin) (右图X轴)
  - Plugin Mode 相对加速比 (右图标题)

修改后:
  - Time (milliseconds, log scale) (左图Y轴)
  - Absolute Time Comparison (左图标题)
  - Speedup Ratio (Integrated / Plugin) (右图X轴)
  - Plugin Mode Relative Speedup (右图标题)
```

### 4. timing_bottleneck_analysis.png
```
修改前:
  - Data Access 优化, Pure Compute 优化 等 (标签)
  - 对总体加速的贡献 (%) (X轴)
  - 性能提升来源分析 (标题)
  - 正贡献（加速）, 负贡献（变慢）(图例)

修改后:
  - Data Access Optimization, Pure Compute Optimization 等 (标签)
  - Contribution to Overall Speedup (%) (X轴)
  - Performance Improvement Source Analysis (标题)
  - Positive (Speedup), Negative (Slowdown) (图例)
```

### 5. timing_summary_table.png
```
修改前: 阶段, 差异, 加速比, 胜者 (表头)
修改后: Phase, Diff, Speedup, Winner (表头)
标题: PECJ Benchmark Complete Timing Metrics Comparison
```

## 字体配置
```python
# 修改前
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial Unicode MS', 'SimHei']

# 修改后
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Helvetica']
```

## 验证结果
```bash
$ ls -lh timing_*.png
-rw-r--r-- 1 cdb cdb 246K Dec 29 19:37 timing_bottleneck_analysis.png
-rw-r--r-- 1 cdb cdb 281K Dec 29 19:37 timing_comparison_bar.png
-rw-r--r-- 1 cdb cdb 245K Dec 29 19:37 timing_speedup.png
-rw-r--r-- 1 cdb cdb 208K Dec 29 19:37 timing_stacked_bar.png
-rw-r--r-- 1 cdb cdb 304K Dec 29 19:37 timing_summary_table.png
```

所有图表已成功生成，文字清晰可读，无乱码问题。

## 优势
1. **跨平台兼容**: 使用英文标签，在任何系统上都能正常显示
2. **国际化友好**: 适合英文学术论文和国际会议投稿
3. **字体独立**: 不依赖特定的中文字体安装
4. **专业外观**: 符合英文学术出版物的标准规范

## 重新生成图表
如需重新生成图表，运行：
```bash
cd /path/to/sageTSDB/examples
python3 visualize_timing.py
```

## 图表说明对照

| 英文标签 | 中文说明 | 用途 |
|---------|---------|------|
| Timing Phase | 时间阶段 | X轴标签 |
| Time (milliseconds) | 时间（毫秒）| Y轴标签 |
| Time Allocation (%) | 时间占比（%）| Y轴标签 |
| Speedup Ratio | 加速比 | X轴标签 |
| Data Access Optimization | 数据访问优化 | 图例项 |
| Pure Compute Optimization | 纯计算优化 | 图例项 |
| Positive (Speedup) | 正贡献（加速）| 图例项 |
| Negative (Slowdown) | 负贡献（变慢）| 图例项 |
| Phase | 阶段 | 表头 |
| Winner | 胜者 | 表头 |

## 关键数据保持不变
尽管标签改为英文，但所有数据和分析结果保持完全一致：
- 59.4% - Data Access optimization contribution
- 17.13× - Data Access speedup
- 1.87× - Overall speedup
- 29.4% → 3.2% - Data Access time proportion change

---

**修复日期**: 2024-12-29
**修复版本**: visualize_timing.py v2.1
**测试状态**: ✅ All charts verified, no encoding issues
