#!/bin/bash
# Build script for sageTSDB with plugin system

echo "=== Building sageTSDB with Plugin System ==="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Determine PECJ directory
PECJ_DIR="../PECJ"
if [ ! -d "$PECJ_DIR" ]; then
    PECJ_DIR="$HOME/dameng/PECJ"
fi

if [ ! -d "$PECJ_DIR" ]; then
    echo -e "${YELLOW}Warning: PECJ directory not found${NC}"
    echo "Tried: ../PECJ and $HOME/dameng/PECJ"
    echo "Building without plugin support"
    PECJ_DIR=""
else
    echo -e "${GREEN}Found PECJ at: $PECJ_DIR${NC}"
fi

# Create build directory
echo -e "${GREEN}Creating build directory...${NC}"
mkdir -p build
cd build

# Configure
echo -e "${GREEN}Configuring CMake...${NC}"
if [ -n "$PECJ_DIR" ]; then
    cmake .. -DCMAKE_BUILD_TYPE=Release -DPECJ_DIR="$PECJ_DIR"
else
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGINS=OFF
fi

if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build
echo ""
echo -e "${GREEN}Building sageTSDB core...${NC}"
make sage_tsdb_core -j$(nproc)

echo ""
echo -e "${GREEN}Building algorithms...${NC}"
make sage_tsdb_algorithms -j$(nproc)

# Build plugins only if target exists
if make --dry-run sage_tsdb_plugins 2>/dev/null | grep -q "Nothing to be done"; then
    echo ""
    echo -e "${GREEN}Building plugins...${NC}"
    make sage_tsdb_plugins -j$(nproc) || echo -e "${YELLOW}Warning: Plugin build failed${NC}"
elif [ -n "$PECJ_DIR" ]; then
    echo ""
    echo -e "${GREEN}Building plugins...${NC}"
    make sage_tsdb_plugins -j$(nproc) || echo -e "${YELLOW}Warning: Plugin build failed (continuing)${NC}"
else
    echo ""
    echo -e "${YELLOW}Skipping plugins (PECJ not found)${NC}"
fi

# Build examples
echo ""
echo -e "${GREEN}Building examples...${NC}"
make -j$(nproc) 2>&1 | grep -v "No rule" || true

# Build tests if available
if [ -n "$PECJ_DIR" ]; then
    echo ""
    echo -e "${GREEN}Building tests...${NC}"
    make test_pecj_plugin test_fault_detection_plugin -j$(nproc) 2>&1 | grep -v "No rule" || echo -e "${YELLOW}Note: Some plugin tests not available${NC}"
fi

echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""

# Show what was built
if [ -f "./lib/libsage_tsdb_core.so" ] || [ -f "./lib/libsage_tsdb_core.a" ]; then
    echo -e "${GREEN}✓ Core library built${NC}"
fi

if [ -f "./lib/libsage_tsdb_algorithms.so" ] || [ -f "./lib/libsage_tsdb_algorithms.a" ]; then
    echo -e "${GREEN}✓ Algorithms library built${NC}"
fi

if [ -f "./lib/libsage_tsdb_plugins.so" ] || [ -f "./lib/libsage_tsdb_plugins.a" ]; then
    echo -e "${GREEN}✓ Plugins library built${NC}"
    echo ""
    echo "Run plugin tests with:"
    echo "  ./tests/test_pecj_plugin"
    echo "  ./tests/test_fault_detection_plugin"
    echo ""
    echo "Run plugin example with:"
    echo "  ./examples/plugin_usage_example"
else
    echo -e "${YELLOW}✗ Plugins not built (PECJ not available)${NC}"
    echo "To build with plugins: export PECJ_DIR=/path/to/PECJ"
fi

echo ""
echo "Run core tests with:"
echo "  ctest --output-on-failure"
echo ""
