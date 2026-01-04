#!/usr/bin/env python3
"""
PECJ Benchmark 细粒度时间分析可视化
基于实际测试结果生成对比图表
"""

import matplotlib.pyplot as plt
import numpy as np
import json
from pathlib import Path

# 实际测试数据
INTEGRATED_MODE = {
    'Total Time': 2444.89,
    'Setup Time': 0.30,
    'Data Preparation': 10.57,
    'Data Access': 718.21,
    'Pure Compute': 1675.83,
    'Result Writing': 11.92,
    'Query Time': 0.00,
    'Cleanup Time': 0.00
}

PLUGIN_MODE = {
    'Total Time': 1306.91,
    'Setup Time': 0.76,
    'Data Preparation': 21.13,
    'Data Access': 41.93,
    'Pure Compute': 1220.68,
    'Result Writing': 0.05,
    'Query Time': 0.01,
    'Cleanup Time': 0.16
}

# 颜色方案
COLORS = {
    'Setup Time': '#FF6B6B',
    'Data Preparation': '#4ECDC4',
    'Data Access': '#FFE66D',
    'Pure Compute': '#95E1D3',
    'Result Writing': '#F38181',
    'Query Time': '#AA96DA',
    'Cleanup Time': '#FCBAD3'
}


def create_comparison_bar_chart():
    """创建细粒度时间对比柱状图"""
    fig, ax = plt.subplots(figsize=(14, 8))
    
    # 排除 Total Time
    phases = ['Data Preparation', 'Data Access', 'Pure Compute', 
              'Result Writing', 'Setup Time', 'Query Time', 'Cleanup Time']
    
    integrated_times = [INTEGRATED_MODE[p] for p in phases]
    plugin_times = [PLUGIN_MODE[p] for p in phases]
    
    x = np.arange(len(phases))
    width = 0.35
    
    bars1 = ax.bar(x - width/2, integrated_times, width, label='Integrated Mode',
                   color='#E74C3C', alpha=0.8, edgecolor='black', linewidth=1.5)
    bars2 = ax.bar(x + width/2, plugin_times, width, label='Plugin Mode',
                   color='#3498DB', alpha=0.8, edgecolor='black', linewidth=1.5)
    
    # 添加数值标签
    for bars in [bars1, bars2]:
        for bar in bars:
            height = bar.get_height()
            if height > 1:
                ax.text(bar.get_x() + bar.get_width()/2., height,
                       f'{height:.1f}',
                       ha='center', va='bottom', fontsize=9, fontweight='bold')
    
    ax.set_xlabel('Timing Phase', fontsize=14, fontweight='bold')
    ax.set_ylabel('Time (milliseconds)', fontsize=14, fontweight='bold')
    ax.set_title('PECJ Benchmark Fine-Grained Timing Comparison\n(20,000 events, 387 windows, 13,856 joins)',
                 fontsize=16, fontweight='bold', pad=20)
    ax.set_xticks(x)
    ax.set_xticklabels(phases, rotation=15, ha='right')
    ax.legend(fontsize=12, loc='upper left')
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    ax.set_yscale('log')  # 对数刻度以显示小数值
    
    plt.tight_layout()
    plt.savefig('timing_comparison_bar.png', dpi=300, bbox_inches='tight')
    print("✓ Generated: timing_comparison_bar.png")


def create_stacked_bar_chart():
    """创建堆叠柱状图显示时间占比"""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    phases = ['Data Preparation', 'Data Access', 'Pure Compute', 
              'Result Writing', 'Setup Time', 'Query Time', 'Cleanup Time']
    
    integrated_times = [INTEGRATED_MODE[p] for p in phases]
    plugin_times = [PLUGIN_MODE[p] for p in phases]
    
    # 计算百分比
    integrated_total = sum(integrated_times)
    plugin_total = sum(plugin_times)
    
    integrated_percent = [(t/integrated_total)*100 for t in integrated_times]
    plugin_percent = [(t/plugin_total)*100 for t in plugin_times]
    
    x = ['Integrated Mode', 'Plugin Mode']
    bottom_integrated = 0
    bottom_plugin = 0
    
    for i, phase in enumerate(phases):
        color = COLORS.get(phase, '#CCCCCC')
        
        # Integrated bar
        ax.bar(0, integrated_percent[i], bottom=bottom_integrated, 
               label=phase if i < len(phases) else '', color=color,
               edgecolor='white', linewidth=2)
        
        # 添加标签（只显示占比>3%的）
        if integrated_percent[i] > 3:
            ax.text(0, bottom_integrated + integrated_percent[i]/2,
                   f'{integrated_percent[i]:.1f}%\n{integrated_times[i]:.1f}ms',
                   ha='center', va='center', fontsize=9, fontweight='bold')
        
        bottom_integrated += integrated_percent[i]
        
        # Plugin bar
        ax.bar(1, plugin_percent[i], bottom=bottom_plugin,
               color=color, edgecolor='white', linewidth=2)
        
        if plugin_percent[i] > 3:
            ax.text(1, bottom_plugin + plugin_percent[i]/2,
                   f'{plugin_percent[i]:.1f}%\n{plugin_times[i]:.1f}ms',
                   ha='center', va='center', fontsize=9, fontweight='bold')
        
        bottom_plugin += plugin_percent[i]
    
    ax.set_ylabel('Time Allocation (%)', fontsize=14, fontweight='bold')
    ax.set_title('PECJ Time Allocation Breakdown\n(Integrated: 2444.89ms  vs  Plugin: 1306.91ms)',
                 fontsize=16, fontweight='bold', pad=20)
    ax.set_xticks([0, 1])
    ax.set_xticklabels(x, fontsize=13)
    ax.set_ylim(0, 100)
    ax.legend(loc='upper left', bbox_to_anchor=(1.02, 1), fontsize=10)
    
    plt.tight_layout()
    plt.savefig('timing_stacked_bar.png', dpi=300, bbox_inches='tight')
    print("✓ Generated: timing_stacked_bar.png")


def create_speedup_chart():
    """创建加速比对比图"""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    
    # 左图：绝对时间对比（对数刻度）
    phases = ['Data\nPreparation', 'Data\nAccess', 'Pure\nCompute', 
              'Result\nWriting', 'Total\nTime']
    
    integrated = [10.57, 718.21, 1675.83, 11.92, 2444.89]
    plugin = [21.13, 41.93, 1220.68, 0.05, 1306.91]
    
    x = np.arange(len(phases))
    width = 0.35
    
    bars1 = ax1.bar(x - width/2, integrated, width, label='Integrated',
                    color='#E74C3C', alpha=0.8)
    bars2 = ax1.bar(x + width/2, plugin, width, label='Plugin',
                    color='#3498DB', alpha=0.8)
    
    ax1.set_ylabel('Time (milliseconds, log scale)', fontsize=12, fontweight='bold')
    ax1.set_title('Absolute Time Comparison', fontsize=14, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(phases, fontsize=10)
    ax1.legend(fontsize=11)
    ax1.set_yscale('log')
    ax1.grid(axis='y', alpha=0.3, linestyle='--')
    
    # 右图：加速比
    speedups = []
    speedup_labels = []
    colors_speedup = []
    
    for i in range(len(integrated)):
        if plugin[i] > 0:
            speedup = integrated[i] / plugin[i]
            speedups.append(speedup)
            speedup_labels.append(phases[i])
            # 根据加速比设置颜色
            if speedup > 10:
                colors_speedup.append('#27AE60')  # 绿色 - 巨大提升
            elif speedup > 2:
                colors_speedup.append('#F39C12')  # 橙色 - 显著提升
            elif speedup > 1:
                colors_speedup.append('#3498DB')  # 蓝色 - 小幅提升
            else:
                colors_speedup.append('#E74C3C')  # 红色 - 变慢
    
    bars = ax2.barh(speedup_labels, speedups, color=colors_speedup, alpha=0.8,
                    edgecolor='black', linewidth=1.5)
    
    # 添加数值标签
    for i, (bar, val) in enumerate(zip(bars, speedups)):
        ax2.text(val + 0.3, bar.get_y() + bar.get_height()/2,
                f'{val:.2f}x',
                va='center', fontsize=11, fontweight='bold')
    
    ax2.axvline(x=1, color='red', linestyle='--', linewidth=2, alpha=0.7,
               label='No Change (1x)')
    ax2.set_xlabel('Speedup Ratio (Integrated / Plugin)', fontsize=12, fontweight='bold')
    ax2.set_title('Plugin Mode Relative Speedup', fontsize=14, fontweight='bold')
    ax2.legend(fontsize=10)
    ax2.grid(axis='x', alpha=0.3, linestyle='--')
    
    plt.tight_layout()
    plt.savefig('timing_speedup.png', dpi=300, bbox_inches='tight')
    print("✓ Generated: timing_speedup.png")


def create_bottleneck_analysis():
    """创建瓶颈分析图"""
    fig, ax = plt.subplots(figsize=(12, 8))
    
    # 计算性能提升的来源
    total_saved = INTEGRATED_MODE['Total Time'] - PLUGIN_MODE['Total Time']
    
    contributions = {
        'Data Access\nOptimization': INTEGRATED_MODE['Data Access'] - PLUGIN_MODE['Data Access'],
        'Pure Compute\nOptimization': INTEGRATED_MODE['Pure Compute'] - PLUGIN_MODE['Pure Compute'],
        'Result Writing\nOptimization': INTEGRATED_MODE['Result Writing'] - PLUGIN_MODE['Result Writing'],
        'Setup Time\nOverhead': -(PLUGIN_MODE['Setup Time'] - INTEGRATED_MODE['Setup Time']),
        'Data Preparation\nOverhead': -(PLUGIN_MODE['Data Preparation'] - INTEGRATED_MODE['Data Preparation'])
    }
    
    # 计算百分比
    percentages = {k: (v/total_saved)*100 for k, v in contributions.items()}
    
    # 按贡献排序
    sorted_items = sorted(percentages.items(), key=lambda x: abs(x[1]), reverse=True)
    labels = [item[0] for item in sorted_items]
    values = [item[1] for item in sorted_items]
    colors = ['#27AE60' if v > 0 else '#E74C3C' for v in values]
    
    bars = ax.barh(labels, values, color=colors, alpha=0.8,
                   edgecolor='black', linewidth=2)
    
    # 添加数值标签
    for bar, val in zip(bars, values):
        x_pos = val + (2 if val > 0 else -2)
        ax.text(x_pos, bar.get_y() + bar.get_height()/2,
               f'{val:+.1f}%',
               va='center', ha='left' if val > 0 else 'right',
               fontsize=12, fontweight='bold')
    
    ax.axvline(x=0, color='black', linewidth=2)
    ax.set_xlabel('Contribution to Overall Speedup (%)', fontsize=14, fontweight='bold')
    ax.set_title(f'Performance Improvement Source Analysis\n(Total Saved: {total_saved:.2f}ms = {(total_saved/INTEGRATED_MODE["Total Time"])*100:.1f}%)',
                 fontsize=16, fontweight='bold', pad=20)
    ax.grid(axis='x', alpha=0.3, linestyle='--')
    
    # 添加图例
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='#27AE60', label='Positive (Speedup)'),
        Patch(facecolor='#E74C3C', label='Negative (Slowdown)')
    ]
    ax.legend(handles=legend_elements, loc='lower right', fontsize=11)
    
    plt.tight_layout()
    plt.savefig('timing_bottleneck_analysis.png', dpi=300, bbox_inches='tight')
    print("✓ Generated: timing_bottleneck_analysis.png")


def create_summary_table():
    """创建摘要表格图"""
    fig, ax = plt.subplots(figsize=(14, 10))
    ax.axis('tight')
    ax.axis('off')
    
    # 表格数据
    phases = ['Data Preparation', 'Data Access', 'Pure Compute', 
              'Result Writing', 'Setup Time', 'Query Time', 
              'Cleanup Time', 'Total Time']
    
    table_data = []
    for phase in phases:
        integ = INTEGRATED_MODE[phase]
        plug = PLUGIN_MODE[phase]
        
        if plug > 0 and integ > 0:
            speedup = integ / plug
            diff_pct = ((plug - integ) / integ) * 100
        elif integ == 0 and plug > 0:
            speedup = 0
            diff_pct = 100
        elif plug == 0 and integ > 0:
            speedup = float('inf')
            diff_pct = -100
        else:
            speedup = 1.0
            diff_pct = 0
        
        # 占比
        integ_pct = (integ / INTEGRATED_MODE['Total Time']) * 100 if phase != 'Total Time' else 100
        plug_pct = (plug / PLUGIN_MODE['Total Time']) * 100 if phase != 'Total Time' else 100
        
        winner = 'Plugin' if plug < integ else 'Integrated'
        
        table_data.append([
            phase,
            f'{integ:.2f}',
            f'{integ_pct:.1f}%',
            f'{plug:.2f}',
            f'{plug_pct:.1f}%',
            f'{diff_pct:+.1f}%',
            f'{speedup:.2f}x',
            winner
        ])
    
    columns = ['Phase', 'Integrated\n(ms)', 'Integ\n(%)', 
               'Plugin\n(ms)', 'Plugin\n(%)', 'Diff', 'Speedup', 'Winner']
    
    table = ax.table(cellText=table_data, colLabels=columns,
                    loc='center', cellLoc='center',
                    colWidths=[0.20, 0.12, 0.10, 0.12, 0.10, 0.10, 0.12, 0.14])
    
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1, 2.5)
    
    # 设置表头样式
    for i in range(len(columns)):
        table[(0, i)].set_facecolor('#34495E')
        table[(0, i)].set_text_props(weight='bold', color='white')
    
    # 设置行样式
    for i in range(1, len(table_data) + 1):
        row_color = '#ECF0F1' if i % 2 == 0 else 'white'
        for j in range(len(columns)):
            table[(i, j)].set_facecolor(row_color)
            
            # 高亮 Total Time
            if table_data[i-1][0] == 'Total Time':
                table[(i, j)].set_facecolor('#F39C12')
                table[(i, j)].set_text_props(weight='bold')
            
            # 高亮胜者
            if j == len(columns) - 1:
                if table_data[i-1][j] == 'Plugin':
                    table[(i, j)].set_text_props(color='#27AE60', weight='bold')
                else:
                    table[(i, j)].set_text_props(color='#E74C3C', weight='bold')
    
    plt.title('PECJ Benchmark Complete Timing Metrics Comparison\n(20,000 events, 387 windows)',
             fontsize=16, fontweight='bold', pad=20)
    
    plt.savefig('timing_summary_table.png', dpi=300, bbox_inches='tight')
    print("✓ Generated: timing_summary_table.png")


def main():
    """生成所有图表"""
    print("\n" + "="*60)
    print("Generating PECJ Benchmark Timing Analysis Charts...")
    print("="*60 + "\n")
    
    # 设置字体 - 使用系统默认英文字体避免乱码
    plt.rcParams['font.family'] = 'sans-serif'
    plt.rcParams['font.sans-serif'] = ['DejaVu Sans', 'Arial', 'Helvetica']
    plt.rcParams['axes.unicode_minus'] = False
    
    try:
        create_comparison_bar_chart()
        create_stacked_bar_chart()
        create_speedup_chart()
        create_bottleneck_analysis()
        create_summary_table()
        
        print("\n" + "="*60)
        print("✅ All charts generated successfully!")
        print("="*60)
        print("\nGenerated files:")
        print("  1. timing_comparison_bar.png     - Fine-grained timing comparison")
        print("  2. timing_stacked_bar.png        - Time allocation breakdown")
        print("  3. timing_speedup.png            - Speedup analysis")
        print("  4. timing_bottleneck_analysis.png- Bottleneck contribution analysis")
        print("  5. timing_summary_table.png      - Complete comparison table")
        
    except Exception as e:
        print(f"\n❌ Error generating charts: {e}")
        import traceback
        traceback.print_exc()


if __name__ == '__main__':
    main()
