#!/bin/bash

# sageTSDB 表设计构建和测试脚本

set -e  # 遇到错误立即退出

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}$1${NC}"
    echo -e "${GREEN}========================================${NC}"
}

print_info() {
    echo -e "${YELLOW}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 解析参数
BUILD_TYPE=${1:-Release}
RUN_TESTS=${2:-true}
RUN_EXAMPLES=${3:-false}

print_header "Building sageTSDB Table Design"
print_info "Build Type: $BUILD_TYPE"
print_info "Run Tests: $RUN_TESTS"
print_info "Run Examples: $RUN_EXAMPLES"

# 创建构建目录
BUILD_DIR="$PROJECT_ROOT/build"
if [ ! -d "$BUILD_DIR" ]; then
    print_info "Creating build directory: $BUILD_DIR"
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# CMake 配置
print_header "Configuring CMake"
cmake .. \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_TESTS=ON \
    -DBUILD_PYTHON_BINDINGS=OFF \
    -DENABLE_OPENMP=ON

if [ $? -ne 0 ]; then
    print_error "CMake configuration failed!"
    exit 1
fi

print_success "CMake configuration completed"

# 编译
print_header "Building Project"
make -j$(nproc) sage_tsdb_core

if [ $? -ne 0 ]; then
    print_error "Build failed!"
    exit 1
fi

print_success "Build completed successfully"

# 编译示例
print_header "Building Examples"
make -j$(nproc) table_design_demo

if [ $? -ne 0 ]; then
    print_error "Example build failed!"
    exit 1
fi

print_success "Examples built successfully"

# 编译测试
if [ "$RUN_TESTS" = "true" ]; then
    print_header "Building Tests"
    make -j$(nproc) test_table_design
    
    if [ $? -ne 0 ]; then
        print_error "Test build failed!"
        exit 1
    fi
    
    print_success "Tests built successfully"
    
    # 运行测试
    print_header "Running Tests"
    
    if [ -f "$BUILD_DIR/tests/test_table_design" ]; then
        print_info "Running test_table_design..."
        "$BUILD_DIR/tests/test_table_design"
        
        if [ $? -eq 0 ]; then
            print_success "All tests passed!"
        else
            print_error "Some tests failed!"
            exit 1
        fi
    else
        print_error "Test executable not found!"
        exit 1
    fi
fi

# 运行示例
if [ "$RUN_EXAMPLES" = "true" ]; then
    print_header "Running Examples"
    
    if [ -f "$BUILD_DIR/examples/table_design_demo" ]; then
        print_info "Running table_design_demo..."
        "$BUILD_DIR/examples/table_design_demo"
        
        if [ $? -eq 0 ]; then
            print_success "Example completed successfully!"
        else
            print_error "Example failed!"
            exit 1
        fi
    else
        print_error "Example executable not found!"
        exit 1
    fi
fi

# 显示构建产物
print_header "Build Artifacts"
print_info "Core Library:"
ls -lh "$BUILD_DIR/lib/libsage_tsdb_core"* 2>/dev/null || print_info "  (not found)"

print_info "Examples:"
ls -lh "$BUILD_DIR/examples/table_design_demo" 2>/dev/null || print_info "  (not found)"

print_info "Tests:"
ls -lh "$BUILD_DIR/tests/test_table_design" 2>/dev/null || print_info "  (not found)"

print_header "Build Summary"
print_success "Build Type: $BUILD_TYPE"
print_success "Build Directory: $BUILD_DIR"
print_success "Status: COMPLETED"

echo ""
print_info "Next steps:"
echo "  1. Run tests:    cd $BUILD_DIR && ctest --output-on-failure"
echo "  2. Run example:  $BUILD_DIR/examples/table_design_demo"
echo "  3. Install:      cd $BUILD_DIR && sudo make install"
echo ""
