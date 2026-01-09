#!/bin/bash
# ============================================================================
# PECJ + sageTSDB Demo 快速启动脚本
# ============================================================================

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认配置
BUILD_DIR="build"
PECJ_DIR="../../../PECJ"
DATA_DIR="${PECJ_DIR}/benchmark/datasets"

# ============================================================================
# 辅助函数
# ============================================================================

print_header() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════════════════╗"
    echo "║            sageTSDB + PECJ Demo Launcher                                ║"
    echo "╚══════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_prerequisites() {
    print_info "Checking prerequisites..."
    
    # 检查构建目录
    if [ ! -d "$BUILD_DIR" ]; then
        print_error "Build directory not found: $BUILD_DIR"
        print_info "Please build the project first:"
        echo "  mkdir -p build && cd build"
        echo "  cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON .."
        echo "  make -j\$(nproc)"
        exit 1
    fi
    
    # 检查可执行文件
    if [ ! -f "$BUILD_DIR/examples/integration/pecj_replay_demo" ]; then
        print_error "Demo executables not found. Please build the project."
        exit 1
    fi
    
    # 检查数据文件
    if [ ! -f "$DATA_DIR/sTuple.csv" ]; then
        print_warning "Data files not found at default location: $DATA_DIR"
        print_info "You can specify custom data paths with --s-file and --r-file"
    else
        print_info "Data files found at: $DATA_DIR"
    fi
    
    print_info "All checks passed!"
    echo ""
}

show_menu() {
    echo -e "${BLUE}Available Demos:${NC}"
    echo ""
    echo "  1) Basic Replay Demo          - 基础流 Join 演示（快速，~5 分钟）"
    echo "  2) Integrated Demo            - PECJ + 故障检测集成（推荐，~10 分钟）"
    echo "  3) Performance Benchmark      - 性能基准测试（较慢，~15-30 分钟）"
    echo "  4) Stock Data Demo            - 股票数据演示（真实场景）"
    echo "  5) High Throughput Demo       - 高吞吐量演示（SHJ 算子）"
    echo "  6) Realtime Simulation        - 实时模拟演示（按时间戳重放）"
    echo ""
    echo "  7) Custom Command             - 自定义命令行参数"
    echo "  8) View Documentation         - 查看完整文档"
    echo "  9) Exit                       - 退出"
    echo ""
}

run_basic_replay() {
    print_info "Running Basic Replay Demo..."
    cd $BUILD_DIR
    ./examples/integration/pecj_replay_demo \
        --s-file "${DATA_DIR}/sTuple.csv" \
        --r-file "${DATA_DIR}/rTuple.csv" \
        --max-tuples 5000 \
        --operator IMA \
        --window-ms 1000
}

run_integrated_demo() {
    print_info "Running Integrated Demo (PECJ + Fault Detection)..."
    cd $BUILD_DIR
    ./examples/integration/integrated_demo \
        --s-file "${DATA_DIR}/sTuple.csv" \
        --r-file "${DATA_DIR}/rTuple.csv" \
        --max-tuples 10000 \
        --detection zscore \
        --threshold 3.0 \
        --output integrated_results.txt
    
    if [ -f "integrated_results.txt" ]; then
        echo ""
        print_info "Results saved to: ${BUILD_DIR}/integrated_results.txt"
    fi
}

run_performance_benchmark() {
    print_info "Running Performance Benchmark..."
    print_warning "This may take 15-30 minutes depending on your system."
    read -p "Continue? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cd $BUILD_DIR
        ./examples/benchmarks/performance_benchmark \
            --s-file "${DATA_DIR}/sTuple.csv" \
            --r-file "${DATA_DIR}/rTuple.csv" \
            --output benchmark_results.csv
        
        if [ -f "benchmark_results.csv" ]; then
            echo ""
            print_info "Results saved to: ${BUILD_DIR}/benchmark_results.csv"
            print_info "You can visualize results using Python/Excel"
        fi
    fi
}

run_stock_demo() {
    print_info "Running Stock Data Demo..."
    local stock_dir="${DATA_DIR}/stock"
    
    if [ ! -d "$stock_dir" ]; then
        print_error "Stock data directory not found: $stock_dir"
        return 1
    fi
    
    cd $BUILD_DIR
    ./examples/integration/integrated_demo \
        --s-file "${stock_dir}/sb_1000ms_1tLowDelayData_short.csv" \
        --r-file "${stock_dir}/cj_1000ms_1tLowDelayData_short.csv" \
        --detection zscore \
        --threshold 2.5
}

run_high_throughput() {
    print_info "Running High Throughput Demo (SHJ operator, 8 threads)..."
    cd $BUILD_DIR
    ./examples/integration/pecj_replay_demo \
        --s-file "${DATA_DIR}/sTuple.csv" \
        --r-file "${DATA_DIR}/rTuple.csv" \
        --max-tuples 50000 \
        --operator SHJ \
        --window-ms 500
}

run_realtime_simulation() {
    print_info "Running Realtime Simulation Demo..."
    print_warning "This demo replays data with real timestamps (slower but more realistic)"
    cd $BUILD_DIR
    ./examples/integration/pecj_replay_demo \
        --s-file "${DATA_DIR}/sTuple.csv" \
        --r-file "${DATA_DIR}/rTuple.csv" \
        --max-tuples 2000 \
        --operator MSWJ \
        --realtime
}

run_custom_command() {
    echo ""
    echo -e "${BLUE}Custom Command Mode${NC}"
    echo "Available programs: pecj_replay_demo, integrated_demo, performance_benchmark"
    echo ""
    echo "Example commands:"
    echo "  ./examples/integration/pecj_replay_demo --help"
    echo "  ./examples/integration/integrated_demo --max-tuples 5000"
    echo ""
    read -p "Enter command: " custom_cmd
    
    if [ -n "$custom_cmd" ]; then
        cd $BUILD_DIR
        eval $custom_cmd
    fi
}

view_documentation() {
    local doc_file="${BUILD_DIR}/examples/README_PECJ_DEMO.md"
    
    if [ -f "$doc_file" ]; then
        if command -v less &> /dev/null; then
            less "$doc_file"
        elif command -v more &> /dev/null; then
            more "$doc_file"
        else
            cat "$doc_file"
        fi
    else
        print_error "Documentation not found at: $doc_file"
        print_info "Please check: examples/README_PECJ_DEMO.md"
    fi
}

# ============================================================================
# 主循环
# ============================================================================

main() {
    print_header
    check_prerequisites
    
    while true; do
        show_menu
        read -p "Select demo (1-9): " choice
        echo ""
        
        case $choice in
            1)
                run_basic_replay
                ;;
            2)
                run_integrated_demo
                ;;
            3)
                run_performance_benchmark
                ;;
            4)
                run_stock_demo
                ;;
            5)
                run_high_throughput
                ;;
            6)
                run_realtime_simulation
                ;;
            7)
                run_custom_command
                ;;
            8)
                view_documentation
                ;;
            9)
                print_info "Exiting. Thank you!"
                exit 0
                ;;
            *)
                print_error "Invalid selection. Please choose 1-9."
                ;;
        esac
        
        echo ""
        echo -e "${GREEN}========================================${NC}"
        echo ""
        read -p "Press Enter to continue..." dummy
        echo ""
    done
}

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --data-dir)
            DATA_DIR="$2"
            shift 2
            ;;
        --quick)
            print_header
            check_prerequisites
            run_basic_replay
            exit 0
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --build-dir <path>   Specify build directory (default: build)"
            echo "  --data-dir <path>    Specify data directory (default: ../../../PECJ/benchmark/datasets)"
            echo "  --quick              Run basic demo immediately and exit"
            echo "  --help               Show this help"
            echo ""
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# 运行主程序
main
