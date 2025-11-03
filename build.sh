#!/bin/bash
# Build script for sageTSDB

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building sageTSDB...${NC}"

# Create build directory
BUILD_DIR="build"
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# Configure
echo -e "${YELLOW}Configuring CMake...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$(cd ..; pwd)/install" \
    -DBUILD_TESTS=ON \
    -DBUILD_PYTHON_BINDINGS=ON \
    -DENABLE_OPENMP=ON

# Build
echo -e "${YELLOW}Building...${NC}"
make -j$(nproc)

# Install to local install directory
echo -e "${YELLOW}Installing to local directory...${NC}"
make install

# Run tests
if [ "$1" == "--test" ]; then
    echo -e "${YELLOW}Running tests...${NC}"
    ctest --output-on-failure
fi

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Libraries installed to: $(cd ..; pwd)/install/lib${NC}"

if [ "$1" == "--install" ]; then
    echo -e "${YELLOW}Installing...${NC}"
    sudo make install
    echo -e "${GREEN}Installation completed!${NC}"
fi
