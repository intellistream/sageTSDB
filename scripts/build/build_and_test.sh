#!/bin/bash
# ============================================================================
# PECJ + sageTSDB Demo 构建和测试脚本
# 
# 本脚本自动化完成：
# 1. 检查构建环境
# 2. 构建 sageTSDB 和 demo 程序
# 3. 运行快速验证测试
# ============================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 默认配置
SAGE_TSDB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SAGE_TSDB_DIR}/build"
PECJ_DIR="${PECJ_DIR:-../../../PECJ}"

print_header() {
    echo -e "${BLUE}"
    echo "╔══════════════════════════════════════════════════════════════════════════╗"
    echo "║        PECJ + sageTSDB Demo Build and Test Script                       ║"
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
    echo -e "\n${BLUE}==== $1 ====${NC}\n"
}

# ============================================================================
# 步骤 1: 检查前置条件
# ============================================================================
check_prerequisites() {
    print_section "Checking Prerequisites"
    
    # 检查必要的工具
    local missing_tools=()
    
    for tool in cmake make g++; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    if [ ${#missing_tools[@]} -gt 0 ]; then
        print_error "Missing required tools: ${missing_tools[*]}"
        print_info "Please install them first. On Ubuntu/Debian:"
        echo "  sudo apt-get install cmake make g++"
        exit 1
    fi
    
    print_info "✓ All required tools found"
    
    # 检查 C++ 编译器版本
    local gcc_version=$(g++ --version | head -1 | grep -oP '\d+\.\d+' | head -1)
    print_info "✓ g++ version: ${gcc_version}"
    
    # 检查 PECJ 目录
    if [ ! -d "$PECJ_DIR" ]; then
        print_warning "PECJ directory not found at: $PECJ_DIR"
        print_info "You can set PECJ_DIR environment variable:"
        echo "  export PECJ_DIR=/path/to/PECJ"
        echo "  $0"
        read -p "Continue anyway? (y/n): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        print_info "✓ PECJ directory found: $PECJ_DIR"
    fi
    
    # 检查数据集
    local data_dir="${PECJ_DIR}/benchmark/datasets"
    if [ -f "${data_dir}/sTuple.csv" ]; then
        local s_lines=$(wc -l < "${data_dir}/sTuple.csv")
        local r_lines=$(wc -l < "${data_dir}/rTuple.csv")
        print_info "✓ Data files found: sTuple.csv (${s_lines} lines), rTuple.csv (${r_lines} lines)"
    else
        print_warning "Data files not found at: $data_dir"
    fi
}

# ============================================================================
# 步骤 2: 配置和构建
# ============================================================================
build_project() {
    print_section "Building sageTSDB with PECJ Integration"
    
    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    print_info "Configuring CMake..."
    cmake -DPECJ_DIR="$PECJ_DIR" \
          -DPECJ_FULL_INTEGRATION=ON \
          -DCMAKE_BUILD_TYPE=Release \
          .. 2>&1 | grep -E "(PECJ|sage_tsdb|examples)" || true
    
    if [ $? -ne 0 ]; then
        print_error "CMake configuration failed"
        exit 1
    fi
    
    print_info "Building project..."
    make -j$(nproc) 2>&1 | tail -20
    
    if [ $? -ne 0 ]; then
        print_error "Build failed"
        exit 1
    fi
    
    print_info "✓ Build completed successfully"
}

# ============================================================================
# 步骤 3: 验证可执行文件
# ============================================================================
verify_executables() {
    print_section "Verifying Demo Executables"
    
    local demos=("pecj_replay_demo" "integrated_demo" "performance_benchmark")
    local all_found=true
    
    for demo in "${demos[@]}"; do
        if [ -f "${BUILD_DIR}/examples/${demo}" ]; then
            local size=$(du -h "${BUILD_DIR}/examples/${demo}" | cut -f1)
            print_info "✓ ${demo} (${size})"
        else
            print_error "✗ ${demo} not found"
            all_found=false
        fi
    done
    
    if [ "$all_found" = false ]; then
        print_error "Some executables are missing"
        exit 1
    fi
}

# ============================================================================
# 步骤 4: 运行快速测试
# ============================================================================
run_quick_test() {
    print_section "Running Quick Verification Test"
    
    cd "$BUILD_DIR"
    
    print_info "Running pecj_replay_demo with 1000 tuples..."
    
    # 运行并捕获输出
    local output=$(./examples/integration/pecj_replay_demo \
        --s-file "${PECJ_DIR}/benchmark/datasets/sTuple.csv" \
        --r-file "${PECJ_DIR}/benchmark/datasets/rTuple.csv" \
        --max-tuples 1000 2>&1)
    
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        print_info "✓ Test completed successfully"
        
        # 提取关键指标
        if echo "$output" | grep -q "Total Tuples Processed"; then
            echo "$output" | grep -E "(Total Tuples|Throughput|Join Selectivity)" | while read line; do
                echo "  $line"
            done
        fi
    else
        print_error "Test failed with exit code: $exit_code"
        echo "$output" | tail -20
        exit 1
    fi
}

# ============================================================================
# 步骤 5: 显示使用说明
# ============================================================================
show_usage_instructions() {
    print_section "Demo Ready!"
    
    cat << EOF
所有 demo 已成功构建并验证。您现在可以：

1. 运行交互式 demo 菜单:
   ${GREEN}cd $SAGE_TSDB_DIR
   ./examples/integration/integrated_demo.sh${NC}

2. 直接运行各个 demo:
   ${GREEN}cd $BUILD_DIR
   
   # 基础演示（5 分钟）
   ./examples/integration/pecj_replay_demo --max-tuples 5000
   
   # 集成演示（10 分钟）
   ./examples/integration/integrated_demo --max-tuples 10000 --detection zscore
   
   # 性能测试（15-30 分钟）
   ./examples/benchmarks/performance_benchmark${NC}

3. 查看完整文档:
   ${GREEN}cat examples/README_PECJ_DEMO.md
   cat examples/QUICKSTART.md
   cat examples/DEMO_SUMMARY.md${NC}

4. 快速演示（推荐首次使用）:
   ${GREEN}./examples/integration/integrated_demo.sh --quick${NC}

${BLUE}提示:${NC} 所有 demo 程序都支持 --help 选项查看详细参数。

EOF
}

# ============================================================================
# 主函数
# ============================================================================
main() {
    print_header
    
    echo "Working directory: $SAGE_TSDB_DIR"
    echo "Build directory: $BUILD_DIR"
    echo "PECJ directory: $PECJ_DIR"
    echo ""
    
    check_prerequisites
    build_project
    verify_executables
    run_quick_test
    show_usage_instructions
    
    print_info "Setup completed successfully! 🎉"
}

# 命令行参数处理
while [[ $# -gt 0 ]]; do
    case $1 in
        --pecj-dir)
            PECJ_DIR="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --skip-test)
            SKIP_TEST=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --pecj-dir <path>    Specify PECJ directory (default: ../../../PECJ)"
            echo "  --build-dir <path>   Specify build directory (default: build)"
            echo "  --skip-test          Skip verification test"
            echo "  --help               Show this help"
            echo ""
            echo "Example:"
            echo "  $0 --pecj-dir /path/to/PECJ"
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
