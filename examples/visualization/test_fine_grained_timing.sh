#!/bin/bash
# 测试细粒度时间测量的 benchmark

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
DATASETS_DIR="$SCRIPT_DIR/datasets"

# 颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║           PECJ Fine-Grained Timing Benchmark Test                         ║${NC}"
echo -e "${GREEN}║           Testing Updated Timing Measurement                              ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# 检查可执行文件
BENCHMARK_BIN="$BUILD_DIR/examples/pecj_integrated_vs_plugin_benchmark"
if [ ! -f "$BENCHMARK_BIN" ]; then
    echo -e "${RED}Error: Benchmark executable not found at $BENCHMARK_BIN${NC}"
    echo "Please compile first with:"
    echo "  cd $BUILD_DIR"
    echo "  make pecj_integrated_vs_plugin_benchmark"
    exit 1
fi

# 检查数据集
S_FILE="$DATASETS_DIR/sTuple.csv"
R_FILE="$DATASETS_DIR/rTuple.csv"
if [ ! -f "$S_FILE" ] || [ ! -f "$R_FILE" ]; then
    echo -e "${YELLOW}Warning: Dataset files not found${NC}"
    echo "Will use synthetic data generation"
fi

# 配置参数
EVENTS=20000
THREADS=4
WINDOW_US=10000
SLIDE_US=5000
REPEAT=1

# 输出文件
OUTPUT_TEXT="$SCRIPT_DIR/fine_grained_timing_results.txt"
OUTPUT_JSON="$SCRIPT_DIR/fine_grained_timing_results.json"

echo -e "${YELLOW}Configuration:${NC}"
echo "  Events        : $EVENTS"
echo "  Threads       : $THREADS"
echo "  Window Length : ${WINDOW_US}us ($(echo "scale=2; $WINDOW_US/1000" | bc)ms)"
echo "  Slide Length  : ${SLIDE_US}us ($(echo "scale=2; $SLIDE_US/1000" | bc)ms)"
echo "  Repetitions   : $REPEAT"
echo ""

echo -e "${GREEN}Running benchmark...${NC}"
echo ""

# 运行 benchmark
"$BENCHMARK_BIN" \
    --s-file "$S_FILE" \
    --r-file "$R_FILE" \
    --events $EVENTS \
    --threads $THREADS \
    --window-us $WINDOW_US \
    --slide-us $SLIDE_US \
    --repeat $REPEAT \
    --output "$OUTPUT_TEXT" \
    --json "$OUTPUT_JSON"

EXIT_CODE=$?

if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║                        Benchmark Complete!                                ║${NC}"
    echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${YELLOW}Results saved to:${NC}"
    echo "  Text format: $OUTPUT_TEXT"
    echo "  JSON format: $OUTPUT_JSON"
    echo ""
    
    # 提取关键指标
    echo -e "${YELLOW}Key Fine-Grained Timing Metrics:${NC}"
    echo ""
    
    if [ -f "$OUTPUT_TEXT" ]; then
        echo "=== Integrated Mode ==="
        grep -A 8 "Integrated Mode" "$OUTPUT_TEXT" | grep -E "(Data Preparation|Data Access|Pure Compute|Result Writing)" | head -4 || true
        echo ""
        
        echo "=== Plugin Mode ==="
        grep -A 8 "Plugin Mode" "$OUTPUT_TEXT" | grep -E "(Data Preparation|Data Access|Pure Compute|Result Writing)" | head -4 || true
        echo ""
        
        echo "=== Performance Breakdown ==="
        grep -A 10 "Performance Breakdown:" "$OUTPUT_TEXT" | tail -8 || true
    fi
else
    echo ""
    echo -e "${RED}╔══════════════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║                        Benchmark Failed!                                  ║${NC}"
    echo -e "${RED}╚══════════════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "Exit code: $EXIT_CODE"
    exit $EXIT_CODE
fi
