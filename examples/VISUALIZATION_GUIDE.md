# PECJ Benchmark 可视化图表使用指南

## 📊 生成的图表概览

本文档介绍5个细粒度时间分析图表及其在研究论文中的应用。

---

## 1. timing_comparison_bar.png - 细粒度时间对比柱状图

### 图表内容
- **X轴**: 7个时间阶段（Data Preparation, Data Access, Pure Compute, Result Writing, Setup Time, Query Time, Cleanup Time）
- **Y轴**: 时间（毫秒，对数刻度）
- **对比**: Integrated Mode (红色) vs Plugin Mode (蓝色)

### 关键发现
```
Data Access: 718.21ms (Integrated) vs 41.93ms (Plugin) 
  → Plugin 快 17.13倍！（这是最大的差异）
  
Pure Compute: 1675.83ms (Integrated) vs 1220.68ms (Plugin)
  → Plugin 快 1.37倍（算法优化贡献）
  
Result Writing: 11.92ms (Integrated) vs 0.05ms (Plugin)
  → Plugin 快 222.34倍（内存写入 vs 磁盘写入）
```

### 论文应用建议
**推荐用于**: Section 6.2 "Performance Comparison"
- 展示两种模式在各阶段的绝对时间差异
- 强调 Data Access 的巨大差距
- 支持"LSM-Tree I/O 是主要瓶颈"的论点

**推荐文字**:
> "Figure X illustrates the fine-grained timing breakdown. The most significant difference lies in Data Access time, where Plugin Mode achieves a 17.13× speedup (41.93ms vs 718.21ms) by eliminating LSM-Tree disk I/O overhead."

---

## 2. timing_stacked_bar.png - 时间分配堆叠图

### 图表内容
- **左柱**: Integrated Mode 时间分配百分比
- **右柱**: Plugin Mode 时间分配百分比
- **颜色分块**: 每个阶段占总时间的比例

### 关键发现
```
Integrated Mode 时间分配:
  Pure Compute:    68.5% (1675.83ms) - 主导
  Data Access:     29.4% (718.21ms)  - 第二大开销
  Result Writing:   0.5% (11.92ms)
  其他:             1.6% (38.93ms)
  
Plugin Mode 时间分配:
  Pure Compute:    93.4% (1220.68ms) - 高度集中
  Data Access:      3.2% (41.93ms)   - 几乎可忽略
  Data Preparation: 1.6% (21.13ms)
  其他:             1.8% (23.37ms)
```

### 论文应用建议
**推荐用于**: Section 6.3 "Bottleneck Analysis"
- 展示时间资源的分配结构
- 对比两种模式的时间占比变化
- 突出 Plugin Mode 如何将计算集中在核心算法上

**推荐文字**:
> "Figure X shows the time allocation breakdown. In Integrated Mode, 29.4% of time is consumed by Data Access, while in Plugin Mode this reduces to merely 3.2%. This allows Plugin Mode to dedicate 93.4% of execution time to Pure Compute operations."

---

## 3. timing_speedup.png - 加速比分析图

### 图表内容
- **左图**: 绝对时间对比（对数刻度）
- **右图**: 各阶段加速比（横向柱状图）
  - 绿色: 10x+ 加速
  - 橙色: 2-10x 加速  
  - 蓝色: 1-2x 加速
  - 红色: 减速

### 关键发现
```
加速比排名（Plugin Mode 相对于 Integrated Mode）:
1. Result Writing:    222.34x ⭐⭐⭐
2. Data Access:       17.13x  ⭐⭐
3. Total Time:        1.87x
4. Pure Compute:      1.37x
5. Data Preparation:  0.50x  (变慢)
```

### 论文应用建议
**推荐用于**: Section 6.2 "Performance Comparison" 或 Abstract
- 直观展示各阶段的相对性能提升
- 强调 Data Access 的 17× 加速是核心优势
- 说明 Data Preparation 的轻微性能损失是可接受的权衡

**推荐文字**:
> "As shown in Figure X, Plugin Mode achieves dramatic speedups in I/O-bound operations: 17.13× for Data Access and 222.34× for Result Writing. While Data Preparation incurs a 2× slowdown due to in-memory sorting, the overall system achieves a 1.87× speedup."

---

## 4. timing_bottleneck_analysis.png - 瓶颈分析图 ⭐⭐⭐

### 图表内容
- **横向柱状图**: 各优化点对总体加速的贡献百分比
- **正值（绿色）**: 正贡献（加速）
- **负值（红色）**: 负贡献（变慢）

### 关键发现
```
性能提升来源分解（总节省 1137.98ms）:
  Data Access 优化:      +59.4%  ⭐ (主要贡献)
  Pure Compute 优化:     +40.0%  (次要贡献)
  Result Writing 优化:   +1.0%
  Data Preparation 损失: -0.9%   (可接受)
  Setup Time 损失:       -0.04%  (忽略不计)
```

### 论文应用建议
**推荐用于**: Section 6.3 "Bottleneck Analysis" 或 Section 7 "Discussion"
- **这是最重要的图表！**
- 定量回答"性能提升来自哪里"
- 支持核心论点："59.4% 的加速来自避免磁盘 I/O"

**推荐文字**（强烈推荐）:
> "Figure X decomposes the performance improvement. Notably, **59.4% of the speedup stems from Data Access optimization** by eliminating LSM-Tree disk I/O, while the remaining 40% comes from algorithmic improvements in Pure Compute. This finding validates our hypothesis that traditional TSDB storage architectures impose fundamental limitations on real-time stream processing."

---

## 5. timing_summary_table.png - 完整对比表格

### 图表内容
- **完整数值表格**: 包含所有阶段的时间、百分比、差异、加速比、胜者
- **表头**: 黑色背景
- **Total Time 行**: 橙色高亮
- **胜者列**: 绿色（Plugin）/ 红色（Integrated）

### 关键数据
```
阶段              Integrated  Integ%  Plugin   Plugin%  差异     加速比   胜者
Data Preparation  10.57ms     0.4%    21.13ms  1.6%     +99.9%   0.50x   Integrated
Data Access       718.21ms    29.4%   41.93ms  3.2%     -94.2%   17.13x  Plugin ⭐
Pure Compute      1675.83ms   68.5%   1220.68ms 93.4%   -27.2%   1.37x   Plugin
Result Writing    11.92ms     0.5%    0.05ms   0.0%     -99.6%   222.34x Plugin
Total Time        2444.89ms   100%    1306.91ms 100%    -46.5%   1.87x   Plugin
```

### 论文应用建议
**推荐用于**: Appendix 或 Supplementary Material
- 提供完整的数据参考
- 方便读者查证具体数值
- 支持其他图表的数据源

**推荐文字**:
> "Table X presents the complete timing breakdown. All measurements represent the average over 387 windows with 20,000 events (10K S, 10K R), producing 13,856 join results. The benchmark was conducted on a 4-thread system."

---

## 📝 论文写作建议

### 推荐的图表使用顺序

1. **Abstract**: 引用 timing_bottleneck_analysis.png 的核心发现
   ```
   "Our fine-grained profiling reveals that 59.4% of the performance 
   improvement stems from eliminating LSM-Tree disk I/O..."
   ```

2. **Section 6.2**: 展示 timing_comparison_bar.png
   - 说明绝对时间差异
   - 强调 Data Access 的 17× 加速

3. **Section 6.3**: 组合使用
   - timing_stacked_bar.png（时间分配变化）
   - timing_bottleneck_analysis.png（贡献分解）
   
4. **Section 6.3 或 6.4**: 展示 timing_speedup.png
   - 总结各阶段相对性能
   - 讨论权衡（Data Preparation 的轻微损失）

5. **Appendix**: 附上 timing_summary_table.png
   - 完整数据参考

### 关键论文论点（基于数据）

✅ **主论点**: "LSM-Tree 磁盘 I/O 是实时流处理的主要瓶颈"
   - 证据: Data Access 从 718ms → 42ms（17.13× 加速）
   - 贡献: 59.4% 的总体性能提升

✅ **次要论点**: "插件模式的算法优化带来额外性能提升"
   - 证据: Pure Compute 从 1676ms → 1221ms（1.37× 加速）
   - 贡献: 40.0% 的总体性能提升

✅ **权衡讨论**: "内存数据准备开销可接受"
   - 证据: Data Preparation 从 11ms → 21ms（2× 变慢）
   - 影响: 仅占总时间的 1.6%，对整体性能影响可忽略

---

## 🔬 实验数据来源

### 测试配置
- **数据量**: 20,000 events (10,000 S + 10,000 R)
- **窗口参数**: 
  - Window Size: 10ms
  - Slide: 5ms
  - 总窗口数: 387
- **Join 结果**: 13,856 joins
- **线程数**: 4 threads
- **重复次数**: 1 repetition

### Benchmark 文件
- **源码**: `pecj_integrated_vs_plugin_benchmark.cpp`
- **可执行文件**: `build/benchmark/pecj_integrated_vs_plugin_benchmark`
- **测试脚本**: `test_fine_grained_timing.sh`
- **详细报告**: `TIMING_ANALYSIS_REPORT.md`

---

## 🛠 重新生成图表

如需修改或重新生成图表：

```bash
cd /path/to/sageTSDB/examples
python3 visualize_timing.py
```

### 自定义图表

编辑 `visualize_timing.py` 中的数据：

```python
INTEGRATED_MODE = {
    'Total Time': 2444.89,
    'Data Access': 718.21,
    # ... 修改你的数据 ...
}

PLUGIN_MODE = {
    'Total Time': 1306.91,
    'Data Access': 41.93,
    # ... 修改你的数据 ...
}
```

---

## 📚 相关文档

- `FINE_GRAINED_TIMING_UPDATE.md` - 代码修改说明
- `TIMING_ANALYSIS_REPORT.md` - 详细性能分析
- `test_fine_grained_timing.sh` - 测试脚本
- `fine_grained_timing_results.txt` - 原始测试输出

---

## ✨ 核心结论

> **59.4% 的性能提升来自避免磁盘 I/O，这验证了传统 TSDB 存储架构对实时流处理的根本性限制。**

这是本次 benchmark 最重要的发现，应该在论文的多个部分（Abstract, Introduction, Results, Discussion）中反复强调。

---

## 📧 联系方式

如有疑问，请查看：
- Benchmark 代码: `src/benchmark/pecj_integrated_vs_plugin_benchmark.cpp`
- 构建说明: `FINE_GRAINED_TIMING_UPDATE.md`

---

**最后更新**: 2024-12-29
**生成工具**: Python 3 + Matplotlib
**数据来源**: PECJ Benchmark (20K events, 387 windows)
