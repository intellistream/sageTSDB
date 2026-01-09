#!/bin/bash
# Build script for sageTSDB

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building sageTSDB...${NC}"

# 确定项目根目录和构建目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 使用项目根目录下的 build/ 目录
BUILD_DIR="${PROJECT_ROOT}/build"
echo -e "${YELLOW}使用构建目录: ${BUILD_DIR}${NC}"

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$PROJECT_ROOT"

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
