#!/usr/bin/env python3
"""
Visualize PECJ Benchmark Results - Fine-Grained Timing Analysis
----------------------------------------------------------------
This script generates a comprehensive visualization chart from fine-grained timing data.

Usage:
    python3 visualize_benchmark.py
    python3 visualize_benchmark.py --output comprehensive_analysis.png

Note: Uses hardcoded fine-grained timing data from recent benchmark runs.
      For 5 separate specialized charts, use visualize_timing.py instead.
"""

import sys
import argparse
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
from pathlib import Path

# Fine-grained timing data from benchmark (20K events, 387 windows, 13,856 joins)
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

# Set style for better looking plots
plt.style.use('seaborn-v0_8-darkgrid')
colors = {
    'integrated': '#E74C3C',
    'plugin': '#3498DB',
    'neutral': '#F39C12',
    'positive': '#27AE60',
    'negative': '#E74C3C'
}

def create_timing_comparison(ax):
    """Create fine-grained timing breakdown comparison chart."""
    phases = ['Data\nPreparation', 'Data\nAccess', 'Pure\nCompute', 
              'Result\nWriting', 'Setup\nTime']
    
    integrated_values = [
        INTEGRATED_MODE['Data Preparation'],
        INTEGRATED_MODE['Data Access'],
        INTEGRATED_MODE['Pure Compute'],
        INTEGRATED_MODE['Result Writing'],
        INTEGRATED_MODE['Setup Time']
    ]
    plugin_values = [
        PLUGIN_MODE['Data Preparation'],
        PLUGIN_MODE['Data Access'],
        PLUGIN_MODE['Pure Compute'],
        PLUGIN_MODE['Result Writing'],
        PLUGIN_MODE['Setup Time']
    ]
    
    x = np.arange(len(phases))
    width = 0.35
    
    bars1 = ax.bar(x - width/2, integrated_values, width, label='Integrated Mode',
                   color=colors['integrated'], alpha=0.8, edgecolor='black', linewidth=1.2)
    bars2 = ax.bar(x + width/2, plugin_values, width, label='Plugin Mode',
                   color=colors['plugin'], alpha=0.8, edgecolor='black', linewidth=1.2)
    
    ax.set_ylabel('Time (ms, log scale)', fontsize=12, fontweight='bold')
    ax.set_title('Fine-Grained Timing Breakdown', fontsize=14, fontweight='bold', pad=20)
    ax.set_xticks(x)
    ax.set_xticklabels(phases, fontsize=9)
    ax.legend(fontsize=10, loc='upper left')
    ax.grid(axis='y', alpha=0.3, linestyle='--')
    ax.set_yscale('log')
    
    # Add value labels on bars (for values > 1ms)
    for bars in [bars1, bars2]:
        for bar in bars:
            height = bar.get_height()
            if height > 1:
                ax.text(bar.get_x() + bar.get_width()/2., height,
                       f'{height:.1f}',
                       ha='center', va='bottom', fontsize=8)

def create_total_time_comparison(ax):
    """Create total execution time comparison with speedup."""
    integrated_total = INTEGRATED_MODE['Total Time']
    plugin_total = PLUGIN_MODE['Total Time']
    
    modes = ['Integrated\nMode', 'Plugin\nMode']
    times = [integrated_total, plugin_total]
    colors_list = [colors['integrated'], colors['plugin']]
    
    bars = ax.bar(modes, times, color=colors_list, alpha=0.8, width=0.6,
                  edgecolor='black', linewidth=1.5)
    
    ax.set_ylabel('Time (ms)', fontsize=12, fontweight='bold')
    ax.set_title('Total Execution Time', fontsize=14, fontweight='bold', pad=20)
    ax.grid(axis='y', alpha=0.3)
    
    # Add value labels
    for bar, time in zip(bars, times):
        height = bar.get_height()
        ax.text(bar.get_x() + bar.get_width()/2., height,
               f'{time:.2f} ms',
               ha='center', va='bottom', fontsize=11, fontweight='bold')
    
    # Add speedup annotation
    speedup = integrated_total / plugin_total if plugin_total > 0 else 0
    winner_text = f'Plugin Mode\n{speedup:.2f}× faster'
    ax.text(0.5, max(times) * 0.85, winner_text,
           ha='center', fontsize=11, fontweight='bold',
           bbox=dict(boxstyle='round,pad=0.5', facecolor='#F39C12', 
                    edgecolor='black', linewidth=2, alpha=0.8))

def create_bottleneck_analysis(ax):
    """Create bottleneck contribution analysis chart."""
    total_saved = INTEGRATED_MODE['Total Time'] - PLUGIN_MODE['Total Time']
    
    contributions = {
        'Data Access\nOptimization': INTEGRATED_MODE['Data Access'] - PLUGIN_MODE['Data Access'],
        'Pure Compute\nOptimization': INTEGRATED_MODE['Pure Compute'] - PLUGIN_MODE['Pure Compute'],
        'Result Writing\nOptimization': INTEGRATED_MODE['Result Writing'] - PLUGIN_MODE['Result Writing'],
        'Data Prep\nOverhead': -(PLUGIN_MODE['Data Preparation'] - INTEGRATED_MODE['Data Preparation']),
        'Setup\nOverhead': -(PLUGIN_MODE['Setup Time'] - INTEGRATED_MODE['Setup Time'])
    }
    
    # Calculate percentages
    percentages = {k: (v/total_saved)*100 for k, v in contributions.items()}
    
    # Sort by absolute value
    sorted_items = sorted(percentages.items(), key=lambda x: abs(x[1]), reverse=True)
    labels = [item[0] for item in sorted_items]
    values = [item[1] for item in sorted_items]
    bar_colors = [colors['positive'] if v > 0 else colors['negative'] for v in values]
    
    bars = ax.barh(labels, values, color=bar_colors, alpha=0.8,
                   edgecolor='black', linewidth=1.2)
    
    # Add value labels
    for bar, val in zip(bars, values):
        x_pos = val + (2 if val > 0 else -2)
        ax.text(x_pos, bar.get_y() + bar.get_height()/2,
               f'{val:+.1f}%',
               va='center', ha='left' if val > 0 else 'right',
               fontsize=10, fontweight='bold')
    
    ax.axvline(x=0, color='black', linewidth=2)
    ax.set_xlabel('Contribution to Speedup (%)', fontsize=12, fontweight='bold')
    ax.set_title('Performance Improvement Sources', fontsize=14, fontweight='bold', pad=20)
    ax.grid(axis='x', alpha=0.3, linestyle='--')
    
    # Add legend
    legend_elements = [
        mpatches.Patch(facecolor=colors['positive'], label='Positive (Speedup)', edgecolor='black'),
        mpatches.Patch(facecolor=colors['negative'], label='Negative (Slowdown)', edgecolor='black')
    ]
    ax.legend(handles=legend_elements, loc='lower right', fontsize=9)
    
def create_speedup_summary(ax):
    """Create speedup summary for key metrics."""
    phases = ['Data\nAccess', 'Pure\nCompute', 'Result\nWriting', 'Total\nTime']
    
    speedups = []
    for phase_key in ['Data Access', 'Pure Compute', 'Result Writing', 'Total Time']:
        integ = INTEGRATED_MODE[phase_key]
        plug = PLUGIN_MODE[phase_key]
        if plug > 0:
            speedups.append(integ / plug)
        else:
            speedups.append(0)
    
    # Color code by speedup magnitude
    bar_colors = []
    for s in speedups:
        if s > 10:
            bar_colors.append('#27AE60')  # Green - huge speedup
        elif s > 2:
            bar_colors.append('#F39C12')  # Orange - significant
        elif s > 1:
            bar_colors.append('#3498DB')  # Blue - moderate
        else:
            bar_colors.append('#E74C3C')  # Red - slower
    
    bars = ax.barh(phases, speedups, color=bar_colors, alpha=0.8,
                   edgecolor='black', linewidth=1.2)
    
    # Add value labels
    for bar, val in zip(bars, speedups):
        ax.text(val + 0.3, bar.get_y() + bar.get_height()/2,
               f'{val:.2f}×',
               va='center', fontsize=10, fontweight='bold')
    
    ax.axvline(x=1, color='red', linestyle='--', linewidth=2, alpha=0.7,
               label='No Change (1×)')
    ax.set_xlabel('Speedup Ratio (Integrated / Plugin)', fontsize=12, fontweight='bold')
    ax.set_title('Plugin Mode Speedup by Phase', fontsize=14, fontweight='bold', pad=20)
    ax.legend(fontsize=9)
    ax.grid(axis='x', alpha=0.3, linestyle='--')


def main():
    """Generate comprehensive visualization."""
    parser = argparse.ArgumentParser(
        description='Generate comprehensive PECJ benchmark visualization'
    )
    parser.add_argument(
        '--output', '-o',
        default='comprehensive_benchmark_analysis.png',
        help='Output filename (default: comprehensive_benchmark_analysis.png)'
    )
    args = parser.parse_args()
    
    print("\n" + "="*70)
    print("  PECJ Benchmark Comprehensive Visualization")
    print("="*70)
    print(f"\nGenerating comprehensive analysis chart...")
    print(f"Output: {args.output}\n")
    
    # Create figure with subplots (2x2 grid)
    fig = plt.figure(figsize=(18, 12))
    gs = fig.add_gridspec(2, 2, hspace=0.3, wspace=0.3)
    
    ax1 = fig.add_subplot(gs[0, 0])  # Top left
    ax2 = fig.add_subplot(gs[0, 1])  # Top right
    ax3 = fig.add_subplot(gs[1, 0])  # Bottom left
    ax4 = fig.add_subplot(gs[1, 1])  # Bottom right
    
    # Create all charts
    create_timing_comparison(ax1)
    create_total_time_comparison(ax2)
    create_bottleneck_analysis(ax3)
    create_speedup_summary(ax4)
    
    # Add overall title
    fig.suptitle('PECJ Benchmark: Integrated vs Plugin Mode - Fine-Grained Analysis\n' +
                 '(20,000 events, 387 windows, 13,856 joins, 4 threads)',
                 fontsize=16, fontweight='bold', y=0.98)
    
    # Add key findings text box
    key_findings = (
        "Key Findings:\n"
        "• 59.4% of speedup from Data Access optimization\n"
        "• 17.13× faster in Data Access (avoiding LSM-Tree I/O)\n"
        "• 1.87× overall speedup (Plugin vs Integrated)\n"
        "• Pure Compute: 1.37× faster with optimized algorithm"
    )
    fig.text(0.02, 0.02, key_findings, fontsize=10, 
            bbox=dict(boxstyle='round', facecolor='lightyellow', 
                     edgecolor='black', linewidth=2, alpha=0.9),
            verticalalignment='bottom')
    
    # Save figure
    plt.savefig(args.output, dpi=300, bbox_inches='tight')
    print(f"✓ Generated: {args.output}")
    
    print("\n" + "="*70)
    print("  Summary")
    print("="*70)
    print(f"Total Time:     Integrated={INTEGRATED_MODE['Total Time']:.2f}ms  "
          f"Plugin={PLUGIN_MODE['Total Time']:.2f}ms")
    print(f"Speedup:        {INTEGRATED_MODE['Total Time']/PLUGIN_MODE['Total Time']:.2f}×")
    print(f"Data Access:    {INTEGRATED_MODE['Data Access']/PLUGIN_MODE['Data Access']:.2f}× faster")
    print(f"Pure Compute:   {INTEGRATED_MODE['Pure Compute']/PLUGIN_MODE['Pure Compute']:.2f}× faster")
    print("="*70 + "\n")


if __name__ == '__main__':
    main()
