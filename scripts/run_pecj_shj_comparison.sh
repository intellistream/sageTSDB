#!/bin/bash

# =============================================================================
# PECJ vs SHJ Comparison Demo Script
# =============================================================================
# 对应示例: examples/pecj_shj_comparison_demo.cpp
# 
# 功能: 比较 PECJ (IMA) 算法与 SHJ (Symmetric Hash Join) 算法的性能
# - 小规模和大规模数据测试
# - 流模式 vs 批处理模式对比
# - 多窗口时间序列 Join 计算
#
# 依赖: 需要构建时启用 PECJ_MODE=INTEGRATED
# =============================================================================

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
EXAMPLES_DIR="${BUILD_DIR}/examples"
PECJ_DIR="/home/cdb/dameng/PECJ"

# 可执行文件路径
EXECUTABLE="${EXAMPLES_DIR}/pecj_shj_comparison_demo"

# 默认数据文件路径
DEFAULT_S_FILE="${PECJ_DIR}/benchmark/datasets/sTuple.csv"
DEFAULT_R_FILE="${PECJ_DIR}/benchmark/datasets/rTuple.csv"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}"
    echo "=============================================================================="
    echo "  PECJ vs SHJ Performance Comparison Demo"
    echo "=============================================================================="
    echo -e "${NC}"
}

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --s-file <path>      Path to S stream data file (default: PECJ datasets)"
    echo "  --r-file <path>      Path to R stream data file (default: PECJ datasets)"
    echo "  --small-count <n>    Number of events for small scale test (default: 5000)"
    echo "  --large-count <n>    Number of events for large scale test (default: 50000)"
    echo "  --threads <n>        Number of threads to use (default: 4)"
    echo "  --build              Rebuild before running"
    echo "  --help               Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Run with defaults"
    echo "  $0 --small-count 1000 --threads 2    # Custom small scale and threads"
    echo "  $0 --build                           # Rebuild and run"
}

# 默认参数
S_FILE="${DEFAULT_S_FILE}"
R_FILE="${DEFAULT_R_FILE}"
SMALL_COUNT=5000
LARGE_COUNT=50000
THREADS=4
DO_BUILD=false

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --s-file)
            S_FILE="$2"
            shift 2
            ;;
        --r-file)
            R_FILE="$2"
            shift 2
            ;;
        --small-count)
            SMALL_COUNT="$2"
            shift 2
            ;;
        --large-count)
            LARGE_COUNT="$2"
            shift 2
            ;;
        --threads)
            THREADS="$2"
            shift 2
            ;;
        --build)
            DO_BUILD=true
            shift
            ;;
        --help)
            print_usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            print_usage
            exit 1
            ;;
    esac
done

print_header

# 检查可执行文件
if [ ! -f "${EXECUTABLE}" ]; then
    echo -e "${YELLOW}Executable not found. Building...${NC}"
    DO_BUILD=true
fi

# 如果需要构建
if [ "$DO_BUILD" = true ]; then
    echo -e "${BLUE}Building project with PECJ integration...${NC}"
    cd "${PROJECT_ROOT}"
    
    if [ ! -d "${BUILD_DIR}" ]; then
        mkdir -p "${BUILD_DIR}"
    fi
    
    cd "${BUILD_DIR}"
    cmake .. \
        -DSAGE_TSDB_ENABLE_PECJ=ON \
        -DPECJ_MODE=INTEGRATED \
        -DPECJ_DIR="${PECJ_DIR}" \
        -DCMAKE_BUILD_TYPE=Release
    
    make -j$(nproc) pecj_shj_comparison_demo
    
    echo -e "${GREEN}Build completed.${NC}"
fi

# 检查数据文件
if [ ! -f "${S_FILE}" ]; then
    echo -e "${RED}Error: S stream data file not found: ${S_FILE}${NC}"
    exit 1
fi

if [ ! -f "${R_FILE}" ]; then
    echo -e "${RED}Error: R stream data file not found: ${R_FILE}${NC}"
    exit 1
fi

# 设置库路径
export LD_LIBRARY_PATH="${PECJ_DIR}/build:${LD_LIBRARY_PATH}"

# 显示运行参数
echo -e "${GREEN}Running PECJ vs SHJ comparison demo...${NC}"
echo ""
echo "Parameters:"
echo "  S Stream File  : ${S_FILE}"
echo "  R Stream File  : ${R_FILE}"
echo "  Small Scale    : ${SMALL_COUNT} events"
echo "  Large Scale    : ${LARGE_COUNT} events"
echo "  Threads        : ${THREADS}"
echo ""
echo "=============================================================================="
echo ""

# 运行演示
cd "${EXAMPLES_DIR}"
"${EXECUTABLE}" \
    --s-file "${S_FILE}" \
    --r-file "${R_FILE}" \
    --small-count "${SMALL_COUNT}" \
    --large-count "${LARGE_COUNT}" \
    --threads "${THREADS}"

echo ""
echo -e "${GREEN}Demo completed successfully!${NC}"
