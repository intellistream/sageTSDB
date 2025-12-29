#!/bin/bash
# run_and_visualize.sh
# 运行 PECJ benchmark 并自动生成细粒度时间分析可视化图表

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build/benchmark"
BENCHMARK_BIN="$BUILD_DIR/pecj_integrated_vs_plugin_benchmark"

# 默认参数
OUTPUT_PREFIX="benchmark_results"
EVENTS=20000
THREADS=4
WINDOW_US=10000
SLIDE_US=5000
OPERATOR="IMA"
REPEAT=1

# 帮助信息
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

运行 PECJ benchmark 并自动生成细粒度时间分析可视化图表

OPTIONS:
    -e, --events N        事件总数 (默认: 20000)
    -t, --threads N       线程数 (默认: 4)
    -w, --window N        窗口长度(微秒) (默认: 10000)
    -s, --slide N         滑动长度(微秒) (默认: 5000)
    -o, --operator TYPE   算子类型 (默认: IMA)
    -r, --repeat N        重复次数 (默认: 1)
    -p, --prefix NAME     输出文件前缀 (默认: benchmark_results)
    -h, --help            显示帮助信息

生成的图表:
    1. timing_comparison_bar.png     - 细粒度时间对比柱状图
    2. timing_stacked_bar.png        - 时间分配堆叠图
    3. timing_speedup.png            - 加速比分析图
    4. timing_bottleneck_analysis.png- 瓶颈贡献分解 ⭐
    5. timing_summary_table.png      - 完整对比表格

示例:
    $0                                    # 使用默认参数
    $0 -e 50000 -t 8                      # 50k 事件，8 线程
    $0 --prefix test1 --repeat 3          # 自定义输出前缀，重复 3 次

EOF
    exit 0
}

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -e|--events)
            EVENTS="$2"
            shift 2
            ;;
        -t|--threads)
            THREADS="$2"
            shift 2
            ;;
        -w|--window)
            WINDOW_US="$2"
            shift 2
            ;;
        -s|--slide)
            SLIDE_US="$2"
            shift 2
            ;;
        -o|--operator)
            OPERATOR="$2"
            shift 2
            ;;
        -r|--repeat)
            REPEAT="$2"
            shift 2
            ;;
        -p|--prefix)
            OUTPUT_PREFIX="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "未知参数: $1"
            usage
            ;;
    esac
done

# 检查 benchmark 程序是否存在
if [ ! -f "$BENCHMARK_BIN" ]; then
    echo "错误: 找不到 benchmark 程序: $BENCHMARK_BIN"
    echo "请先编译: cd ../build && cmake .. && make pecj_integrated_vs_plugin_benchmark"
    exit 1
fi

# 检查 Python 脚本是否存在
if [ ! -f "$SCRIPT_DIR/visualize_timing.py" ]; then
    echo "错误: 找不到可视化脚本: $SCRIPT_DIR/visualize_timing.py"
    exit 1
fi

# 检查依赖
if ! python3 -c "import matplotlib, numpy" 2>/dev/null; then
    echo "警告: 缺少 Python 依赖，正在安装..."
    pip install matplotlib numpy -q
fi

# 设置输出文件
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
TXT_FILE="$SCRIPT_DIR/${OUTPUT_PREFIX}_${TIMESTAMP}.txt"

echo "=================================================="
echo "  PECJ Benchmark Fine-Grained Timing Analysis"
echo "=================================================="
echo "配置:"
echo "  事件数: $EVENTS"
echo "  线程数: $THREADS"
echo "  窗口长度: $(echo "scale=2; $WINDOW_US/1000" | bc) ms"
echo "  滑动长度: $(echo "scale=2; $SLIDE_US/1000" | bc) ms"
echo "  算子类型: $OPERATOR"
echo "  重复次数: $REPEAT"
echo ""
echo "输出文件:"
echo "  文本日志: $TXT_FILE"
echo "  图表输出: timing_*.png (5个图表)"
echo "=================================================="
echo ""

# 运行 benchmark
echo "[1/2] 运行 benchmark (细粒度时间测量)..."
"$BENCHMARK_BIN" \
    --events "$EVENTS" \
    --threads "$THREADS" \
    --window-us "$WINDOW_US" \
    --slide-us "$SLIDE_US" \
    --operator "$OPERATOR" \
    --repeat "$REPEAT" \
    2>&1 | tee "$TXT_FILE"

if [ $? -ne 0 ]; then
    echo "错误: benchmark 运行失败"
    exit 1
fi

echo ""
echo "[2/2] 生成细粒度时间分析图表..."
cd "$SCRIPT_DIR"
python3 visualize_timing.py

if [ $? -ne 0 ]; then
    echo "错误: 可视化生成失败"
    exit 1
fi

echo ""
echo "=================================================="
echo "  完成! 结果文件:"
echo "=================================================="
echo "  📊 图表 (5个):"
echo "     1. timing_comparison_bar.png       - Fine-grained timing comparison"
echo "     2. timing_stacked_bar.png          - Time allocation breakdown"
echo "     3. timing_speedup.png              - Speedup analysis"
echo "     4. timing_bottleneck_analysis.png  - Bottleneck analysis ⭐"
echo "     5. timing_summary_table.png        - Complete comparison table"
echo ""
echo "  � 文本日志: $TXT_FILE"
echo "=================================================="
echo ""
echo "关键发现:"
echo "  • 59.4% of speedup from Data Access optimization"
echo "  • 17.13× faster in Data Access (avoiding LSM-Tree I/O)"
echo "  • 1.87× overall speedup (Plugin vs Integrated)"
echo ""
echo "查看图表:"
echo "  xdg-open timing_bottleneck_analysis.png  # 最重要!"
echo "  xdg-open timing_comparison_bar.png"
echo ""
