#!/bin/bash
# compare_pecj_modes.sh
# Script to compare PLUGIN mode vs INTEGRATED mode performance

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}PECJ Mode Comparison: PLUGIN vs INTEGRATED${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""

# Configuration
PECJ_DIR=${PECJ_DIR:-/home/cdb/dameng/PECJ}
BUILD_TYPE=${BUILD_TYPE:-Release}
NUM_JOBS=${NUM_JOBS:-$(nproc)}

# ========== Build Plugin Mode ==========

echo -e "${YELLOW}Step 1: Building PLUGIN mode...${NC}"
mkdir -p build_plugin
cd build_plugin

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=PLUGIN \
    -DPECJ_DIR="$PECJ_DIR" \
    -DBUILD_PECJ_EXAMPLES=ON

make -j"$NUM_JOBS"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ PLUGIN mode build successful${NC}"
else
    echo -e "${RED}✗ PLUGIN mode build failed${NC}"
    exit 1
fi

cd ..

# ========== Build Integrated Mode ==========

echo ""
echo -e "${YELLOW}Step 2: Building INTEGRATED mode...${NC}"
mkdir -p build_integrated
cd build_integrated

cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DSAGE_TSDB_ENABLE_PECJ=ON \
    -DPECJ_MODE=INTEGRATED \
    -DPECJ_DIR="$PECJ_DIR" \
    -DBUILD_PECJ_EXAMPLES=ON

make -j"$NUM_JOBS"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ INTEGRATED mode build successful${NC}"
else
    echo -e "${RED}✗ INTEGRATED mode build failed${NC}"
    exit 1
fi

cd ..

# ========== Run Benchmarks ==========

echo ""
echo -e "${YELLOW}Step 3: Running benchmarks...${NC}"
echo ""

# Plugin mode benchmark
echo -e "${BLUE}Running PLUGIN mode benchmark...${NC}"
if [ -f "build_plugin/pecj_benchmark" ]; then
    ./build_plugin/pecj_benchmark > results_plugin.txt 2>&1 || true
    echo -e "${GREEN}✓ PLUGIN mode benchmark completed${NC}"
else
    echo -e "${YELLOW}⚠ PLUGIN mode benchmark not found, skipping${NC}"
fi

# Integrated mode benchmark
echo -e "${BLUE}Running INTEGRATED mode benchmark...${NC}"
if [ -f "build_integrated/pecj_benchmark" ]; then
    ./build_integrated/pecj_benchmark > results_integrated.txt 2>&1 || true
    echo -e "${GREEN}✓ INTEGRATED mode benchmark completed${NC}"
else
    echo -e "${YELLOW}⚠ INTEGRATED mode benchmark not found, skipping${NC}"
fi

# ========== Compare Results ==========

echo ""
echo -e "${YELLOW}Step 4: Comparing results...${NC}"
echo ""

if [ -f "results_plugin.txt" ] && [ -f "results_integrated.txt" ]; then
    echo -e "${BLUE}=== Performance Comparison ===${NC}"
    echo ""
    
    # Extract key metrics (this is a placeholder - actual parsing depends on output format)
    echo -e "${GREEN}PLUGIN Mode:${NC}"
    grep -E "(Throughput|Latency|Memory)" results_plugin.txt | head -5 || echo "  (metrics not found)"
    
    echo ""
    echo -e "${GREEN}INTEGRATED Mode:${NC}"
    grep -E "(Throughput|Latency|Memory)" results_integrated.txt | head -5 || echo "  (metrics not found)"
    
    echo ""
    echo "Full results saved to:"
    echo "  - results_plugin.txt"
    echo "  - results_integrated.txt"
else
    echo -e "${YELLOW}⚠ Benchmark results not found${NC}"
fi

# ========== File Size Comparison ==========

echo ""
echo -e "${YELLOW}Step 5: Binary size comparison...${NC}"
echo ""

PLUGIN_LIB="build_plugin/libsage_tsdb_pecj_plugin.a"
INTEGRATED_LIB="build_integrated/libsage_tsdb_pecj_engine.a"

if [ -f "$PLUGIN_LIB" ]; then
    PLUGIN_SIZE=$(du -h "$PLUGIN_LIB" | cut -f1)
    echo -e "PLUGIN library size: ${BLUE}$PLUGIN_SIZE${NC}"
fi

if [ -f "$INTEGRATED_LIB" ]; then
    INTEGRATED_SIZE=$(du -h "$INTEGRATED_LIB" | cut -f1)
    echo -e "INTEGRATED library size: ${BLUE}$INTEGRATED_SIZE${NC}"
fi

# ========== Summary ==========

echo ""
echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}Comparison Summary${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""

echo "Build Directories:"
echo "  PLUGIN mode:     build_plugin/"
echo "  INTEGRATED mode: build_integrated/"
echo ""

echo "Result Files:"
echo "  PLUGIN results:     results_plugin.txt"
echo "  INTEGRATED results: results_integrated.txt"
echo ""

echo -e "${GREEN}Comparison completed!${NC}"
echo ""

echo "To visualize results, run:"
echo "  python3 scripts/plot_comparison.py results_plugin.txt results_integrated.txt"
echo ""
