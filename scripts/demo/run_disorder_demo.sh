#!/bin/bash
# =============================================================================
# Disorder Handling Demonstration Script
# =============================================================================
# 对应示例: examples/integration/deep_integration_demo.cpp
#
# 功能: 演示 sageTSDB + PECJ 在不同乱序场景下的处理能力
# - 快速演示模式 (quick)
# - 完整测试模式 (full)
# - 性能基准测试 (benchmark)
#
# 合并自: benchmark_disorder.sh, demo_disorder_showcase.sh, run_high_disorder_demo.sh
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${PROJECT_ROOT}/build"
DEMO_BIN="${BUILD_DIR}/examples/deep_integration_demo"
DATASETS_DIR="${PROJECT_ROOT}/examples/datasets"
RESULTS_DIR="${BUILD_DIR}/disorder_results"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# =============================================================================
# Helper Functions
# =============================================================================

print_header() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════════════════╗"
    echo "║         sageTSDB + PECJ Disorder Handling Demonstration                 ║"
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

print_section() {
    echo ""
    echo -e "${CYAN}═══ $1 ═══${NC}"
    echo ""
}

check_prerequisites() {
    print_info "Checking prerequisites..."
    
    if [ ! -f "$DEMO_BIN" ]; then
        print_error "Demo executable not found: $DEMO_BIN"
        print_info "Please build the project first:"
        echo "  ./scripts/build/build.sh"
        exit 1
    fi
    
    if [ ! -f "${DATASETS_DIR}/sTuple.csv" ] || [ ! -f "${DATASETS_DIR}/rTuple.csv" ]; then
        print_error "Dataset files not found in: $DATASETS_DIR"
        exit 1
    fi
    
    mkdir -p "$RESULTS_DIR"
    print_info "Results will be saved to: $RESULTS_DIR"
    print_info "All checks passed!"
}

# =============================================================================
# Demo Modes
# =============================================================================

run_quick_demo() {
    print_section "Quick Disorder Demo (5 minutes)"
    
    print_info "Testing disorder rates: 10%, 20%, 30%"
    
    for disorder_rate in 0.1 0.2 0.3; do
        local rate_percent=$(echo "$disorder_rate * 100" | bc)
        print_info "Running with ${rate_percent}% disorder rate..."
        
        "$DEMO_BIN" \
            --s-file "${DATASETS_DIR}/sTuple.csv" \
            --r-file "${DATASETS_DIR}/rTuple.csv" \
            --max-s 10000 \
            --max-r 10000 \
            --disorder true \
            --disorder-ratio "$disorder_rate" \
            --window-us 1000000 \
            --slide-us 500000
        
        echo ""
    done
    
    print_info "Quick demo completed!"
}

run_full_demo() {
    print_section "Full Disorder Test (15 minutes)"
    
    print_info "Testing multiple scenarios with detailed metrics..."
    
    local scenarios=(
        "0.1:20000:20000:Low Disorder (10%)"
        "0.2:50000:50000:Medium Disorder (20%)"
        "0.3:100000:100000:High Disorder (30%)"
        "0.5:100000:100000:Very High Disorder (50%)"
    )
    
    for scenario in "${scenarios[@]}"; do
        IFS=':' read -r disorder max_s max_r desc <<< "$scenario"
        
        print_info "Running: $desc"
        
        "$DEMO_BIN" \
            --s-file "${DATASETS_DIR}/sTuple.csv" \
            --r-file "${DATASETS_DIR}/rTuple.csv" \
            --max-s "$max_s" \
            --max-r "$max_r" \
            --disorder true \
            --disorder-ratio "$disorder" \
            --window-us 1000000 \
            --slide-us 500000
        
        echo ""
    done
    
    print_info "Full test completed!"
    print_info "Results can be analyzed from console output"
}

run_benchmark() {
    print_section "Disorder Performance Benchmark (30 minutes)"
    
    print_info "Running comprehensive performance benchmark..."
    print_warning "This will take approximately 30 minutes"
    
    local benchmark_file="${RESULTS_DIR}/disorder_benchmark_$(date +%Y%m%d_%H%M%S).csv"
    
    echo "DisorderRate,MaxS,MaxR,Description" > "$benchmark_file"
    
    local disorder_rates=(0.0 0.1 0.2 0.3 0.4 0.5)
    local data_scales=(
        "10000:10000:Small"
        "50000:50000:Medium"
        "100000:100000:Large"
        "200000:200000:XLarge"
    )
    
    for disorder in "${disorder_rates[@]}"; do
        for scale in "${data_scales[@]}"; do
            IFS=':' read -r max_s max_r desc <<< "$scale"
            
            local rate_percent=$(echo "$disorder * 100" | bc)
            print_info "Testing ${rate_percent}% disorder with $desc dataset..."
            
            "$DEMO_BIN" \
                --s-file "${DATASETS_DIR}/sTuple.csv" \
                --r-file "${DATASETS_DIR}/rTuple.csv" \
                --max-s "$max_s" \
                --max-r "$max_r" \
                --disorder true \
                --disorder-ratio "$disorder" \
                --window-us 1000000 \
                --slide-us 500000 \
                --quiet
            
            echo "$disorder,$max_s,$max_r,$desc" >> "$benchmark_file"
            
            sleep 1
        done
    done
    
    print_info "Benchmark completed!"
    print_info "Results saved to: $benchmark_file"
    echo ""
    print_info "You can analyze results using:"
    echo "  - Excel/LibreOffice Calc"
    echo "  - Python: pandas.read_csv('$benchmark_file')"
    echo "  - R: read.csv('$benchmark_file')"
}

# =============================================================================
# Main Menu
# =============================================================================

show_menu() {
    echo -e "${BLUE}Available Modes:${NC}"
    echo ""
    echo "  1) Quick Demo        - Quick disorder test (~5 min)"
    echo "  2) Full Demo         - Comprehensive test (~15 min)"
    echo "  3) Benchmark         - Performance benchmark (~30 min)"
    echo "  4) All               - Run all modes sequentially"
    echo "  5) Exit"
    echo ""
}

main() {
    print_header
    check_prerequisites
    
    if [ $# -eq 0 ]; then
        while true; do
            show_menu
            read -p "Select mode [1-5]: " choice
            
            case $choice in
                1)
                    run_quick_demo
                    break
                    ;;
                2)
                    run_full_demo
                    break
                    ;;
                3)
                    run_benchmark
                    break
                    ;;
                4)
                    run_quick_demo
                    run_full_demo
                    run_benchmark
                    break
                    ;;
                5)
                    print_info "Exiting..."
                    exit 0
                    ;;
                *)
                    print_error "Invalid choice. Please select 1-5."
                    ;;
            esac
        done
    else
        case $1 in
            quick)
                run_quick_demo
                ;;
            full)
                run_full_demo
                ;;
            benchmark)
                run_benchmark
                ;;
            all)
                run_quick_demo
                run_full_demo
                run_benchmark
                ;;
            *)
                echo "Usage: $0 [quick|full|benchmark|all]"
                echo ""
                echo "Modes:"
                echo "  quick      - Quick disorder test (~5 min)"
                echo "  full       - Comprehensive test (~15 min)"
                echo "  benchmark  - Performance benchmark (~30 min)"
                echo "  all        - Run all modes sequentially"
                echo ""
                echo "If no mode specified, interactive menu will be shown."
                exit 1
                ;;
        esac
    fi
    
    echo ""
    print_info "═══ Demo Completed Successfully! ═══"
}

main "$@"
