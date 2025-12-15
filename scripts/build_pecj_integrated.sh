#!/bin/bash
# build_pecj_integrated.sh
# Quick build script for PECJ Deep Integration Mode

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}PECJ Deep Integration Mode Build Script${NC}"
echo -e "${GREEN}================================================${NC}"

# ========== Configuration ==========

# Default values
BUILD_TYPE=${BUILD_TYPE:-Release}
PECJ_DIR=${PECJ_DIR:-/home/cdb/dameng/PECJ}
BUILD_DIR=${BUILD_DIR:-build_integrated}
ENABLE_EXAMPLES=${ENABLE_EXAMPLES:-ON}
ENABLE_TESTS=${ENABLE_TESTS:-ON}
NUM_JOBS=${NUM_JOBS:-$(nproc)}

echo -e "${YELLOW}Configuration:${NC}"
echo "  Build Type: $BUILD_TYPE"
echo "  PECJ Directory: $PECJ_DIR"
echo "  Build Directory: $BUILD_DIR"
echo "  Enable Examples: $ENABLE_EXAMPLES"
echo "  Enable Tests: $ENABLE_TESTS"
echo "  Parallel Jobs: $NUM_JOBS"
echo ""

# ========== Check PECJ Directory ==========

if [ ! -d "$PECJ_DIR" ]; then
    echo -e "${RED}Error: PECJ directory not found: $PECJ_DIR${NC}"
    echo "Please set PECJ_DIR environment variable or update the script"
    exit 1
fi

echo -e "${GREEN}✓ PECJ directory found${NC}"

# ========== Create Build Directory ==========

echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# ========== Run CMake ==========

echo -e "${YELLOW}Running CMake configuration...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR="$PECJ_DIR" \
    -DBUILD_PECJ_EXAMPLES="$ENABLE_EXAMPLES" \
    -DBUILD_PECJ_TESTS="$ENABLE_TESTS"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ CMake configuration successful${NC}"
else
    echo -e "${RED}✗ CMake configuration failed${NC}"
    exit 1
fi

# ========== Build ==========

echo -e "${YELLOW}Building PECJ Compute Engine...${NC}"
make -j"$NUM_JOBS"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi

# ========== Run Tests (if enabled) ==========

if [ "$ENABLE_TESTS" = "ON" ]; then
    echo -e "${YELLOW}Running tests...${NC}"
    ctest --output-on-failure
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ All tests passed${NC}"
    else
        echo -e "${YELLOW}⚠ Some tests failed (see output above)${NC}"
    fi
fi

# ========== Run Example (if enabled) ==========

if [ "$ENABLE_EXAMPLES" = "ON" ]; then
    if [ -f "./pecj_compute_example" ]; then
        echo -e "${YELLOW}Running example...${NC}"
        ./pecj_compute_example
        
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}✓ Example executed successfully${NC}"
        else
            echo -e "${YELLOW}⚠ Example execution had issues${NC}"
        fi
    fi
fi

# ========== Summary ==========

echo ""
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}Build Summary${NC}"
echo -e "${GREEN}================================================${NC}"
echo "Build directory: $(pwd)"
echo ""
echo "Generated libraries:"
if [ -f "./libsage_tsdb_pecj_engine.a" ]; then
    echo -e "  ${GREEN}✓${NC} libsage_tsdb_pecj_engine.a"
else
    echo -e "  ${RED}✗${NC} libsage_tsdb_pecj_engine.a (not found)"
fi

echo ""
echo "Generated executables:"
if [ -f "./pecj_compute_example" ]; then
    echo -e "  ${GREEN}✓${NC} pecj_compute_example"
fi
if [ -f "./pecj_compute_engine_test" ]; then
    echo -e "  ${GREEN}✓${NC} pecj_compute_engine_test"
fi

echo ""
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""
echo "To run the example:"
echo "  cd $BUILD_DIR && ./pecj_compute_example"
echo ""
echo "To run tests:"
echo "  cd $BUILD_DIR && ctest --verbose"
echo ""
