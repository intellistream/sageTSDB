#!/bin/bash
# Build script for sageTSDB

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building sageTSDB...${NC}"

# 确定构建目录：优先使用 .sage/build/sage_tsdb（统一构建目录）
# 如果在 middleware 上下文中构建，会由父 CMake 管理
# 如果独立构建（开发/测试），则使用本地 build/
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 路径: sageTSDB -> sage_tsdb -> components -> middleware -> sage -> src -> sage-middleware -> packages -> SAGE
SAGE_ROOT="$(cd "${SCRIPT_DIR}/../../../../../../../.." && pwd)"

if [ -d "${SAGE_ROOT}/.sage" ]; then
    # 在 SAGE 项目根目录下，使用统一构建目录
    BUILD_DIR="${SAGE_ROOT}/.sage/build/sage_tsdb"
    echo -e "${YELLOW}使用统一构建目录: ${BUILD_DIR}${NC}"
else
    # 独立构建（子模块开发模式）
    BUILD_DIR="${SCRIPT_DIR}/build"
    echo -e "${YELLOW}使用本地构建目录: ${BUILD_DIR}${NC}"
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$SCRIPT_DIR"

# Configure
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install" \
    -DBUILD_TESTS=ON \
    -DBUILD_PYTHON_BINDINGS=ON \
    -DENABLE_OPENMP=ON

# Build
echo -e "${YELLOW}Building...${NC}"
cmake --build "$BUILD_DIR" -j$(nproc)

# Install to local install directory
echo -e "${YELLOW}Installing to local directory...${NC}"
cmake --build "$BUILD_DIR" --target install

# Run tests
if [ "$1" == "--test" ]; then
    echo -e "${YELLOW}Running tests...${NC}"
    cd "$BUILD_DIR"
    ctest --output-on-failure
    cd "$SCRIPT_DIR"
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Libraries installed to: ${BUILD_DIR}/install/lib${NC}"

if [ "$1" == "--install" ]; then
    echo -e "${YELLOW}Installing...${NC}"
    sudo make install
    echo -e "${GREEN}Installation completed!${NC}"
fi
