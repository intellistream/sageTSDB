#!/bin/bash
# Run High Disorder & Large Scale Demo for sageTSDB + PECJ

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"
DEMO_BIN="${BUILD_DIR}/examples/deep_integration_demo"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  sageTSDB + PECJ: High Disorder & Large Scale Test Suite     ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Check if demo binary exists
if [ ! -f "$DEMO_BIN" ]; then
    echo -e "${RED}❌ Demo binary not found: $DEMO_BIN${NC}"
    echo -e "${YELLOW}Please build the project first:${NC}"
    echo "  cd $PROJECT_ROOT"
    echo "  ./scripts/build_pecj_integrated.sh"
    exit 1
fi

# Check if PECJ datasets exist - try multiple possible locations
PECJ_DATA_DIR=""
for dir in "${PROJECT_ROOT}/../../../PECJ/benchmark/datasets" \
           "${PROJECT_ROOT}/examples/datasets" \
           "/home/cdb/dameng/PECJ/benchmark/datasets"; do
    if [ -d "$dir" ]; then
        PECJ_DATA_DIR="$dir"
        break
    fi
done

if [ -z "$PECJ_DATA_DIR" ]; then
    echo -e "${RED}❌ PECJ dataset directory not found!${NC}"
    echo "Tried:"
    echo "  ${PROJECT_ROOT}/../../../PECJ/benchmark/datasets"
    echo "  ${PROJECT_ROOT}/examples/datasets"
    echo "  /home/cdb/dameng/PECJ/benchmark/datasets"
    exit 1
fi

echo -e "${GREEN}✓ Found datasets in: $PECJ_DATA_DIR${NC}"
S_FILE="${PECJ_DATA_DIR}/sTuple.csv"
R_FILE="${PECJ_DATA_DIR}/rTuple.csv"

# Function to run a test scenario
run_test() {
    local test_name="$1"
    shift
    local args="$@"
    
    echo ""
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}Test: $test_name${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}Command:${NC} $DEMO_BIN $args"
    echo ""
    
    $DEMO_BIN $args
    
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}✓ Test completed successfully${NC}"
    else
        echo -e "${RED}✗ Test failed with exit code: $exit_code${NC}"
    fi
    
    echo ""
    echo -e "${BLUE}Press Enter to continue to next test (or Ctrl+C to exit)...${NC}"
    read
}

# Parse command line arguments
TEST_MODE="all"
if [ $# -gt 0 ]; then
    TEST_MODE="$1"
fi

case "$TEST_MODE" in
    "baseline")
        echo -e "${YELLOW}Running baseline test (no disorder)...${NC}"
        run_test "Baseline: No Disorder" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 100000 --max-r 100000 \
            --disorder false \
            --threads 8
        ;;
    
    "low-disorder")
        echo -e "${YELLOW}Running low disorder test...${NC}"
        run_test "Low Disorder: 10% disorder, 2ms delay" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 100000 --max-r 100000 \
            --disorder true \
            --disorder-ratio 0.1 \
            --max-disorder-us 2000 \
            --threads 8
        ;;
    
    "medium-disorder")
        echo -e "${YELLOW}Running medium disorder test...${NC}"
        run_test "Medium Disorder: 30% disorder, 5ms delay" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 150000 --max-r 150000 \
            --disorder true \
            --disorder-ratio 0.3 \
            --max-disorder-us 5000 \
            --threads 8
        ;;
    
    "high-disorder")
        echo -e "${YELLOW}Running high disorder test...${NC}"
        run_test "High Disorder: 50% disorder, 10ms delay" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 200000 --max-r 200000 \
            --disorder true \
            --disorder-ratio 0.5 \
            --max-disorder-us 10000 \
            --threads 8
        ;;
    
    "extreme-disorder")
        echo -e "${YELLOW}Running extreme disorder test...${NC}"
        run_test "Extreme Disorder: 70% disorder, 20ms delay" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 200000 --max-r 200000 \
            --disorder true \
            --disorder-ratio 0.7 \
            --max-disorder-us 20000 \
            --threads 8
        ;;
    
    "large-scale")
        echo -e "${YELLOW}Running large scale test...${NC}"
        run_test "Large Scale: 500K events with 30% disorder" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 250000 --max-r 250000 \
            --disorder true \
            --disorder-ratio 0.3 \
            --max-disorder-us 5000 \
            --threads 8
        ;;
    
    "stress-test")
        echo -e "${YELLOW}Running stress test...${NC}"
        run_test "Stress Test: Max events, high disorder, many threads" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 300000 --max-r 300000 \
            --disorder true \
            --disorder-ratio 0.6 \
            --max-disorder-us 15000 \
            --threads 16
        ;;
    
    "all")
        echo -e "${YELLOW}Running all test scenarios...${NC}"
        
        run_test "1. Baseline (No Disorder)" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 100000 --max-r 100000 \
            --disorder false \
            --threads 8
        
        run_test "2. Low Disorder (10%)" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 100000 --max-r 100000 \
            --disorder true \
            --disorder-ratio 0.1 \
            --max-disorder-us 2000 \
            --threads 8
        
        run_test "3. Medium Disorder (30%)" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 150000 --max-r 150000 \
            --disorder true \
            --disorder-ratio 0.3 \
            --max-disorder-us 5000 \
            --threads 8
        
        run_test "4. High Disorder (50%)" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 200000 --max-r 200000 \
            --disorder true \
            --disorder-ratio 0.5 \
            --max-disorder-us 10000 \
            --threads 8
        
        run_test "5. Large Scale (500K events)" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 250000 --max-r 250000 \
            --disorder true \
            --disorder-ratio 0.3 \
            --max-disorder-us 5000 \
            --threads 8
        
        echo ""
        echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║            All tests completed successfully!                  ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
        ;;
    
    "quick")
        echo -e "${YELLOW}Running quick test (smaller datasets)...${NC}"
        run_test "Quick Test: 50K events, 30% disorder" \
            --s-file "$S_FILE" --r-file "$R_FILE" \
            --max-s 25000 --max-r 25000 \
            --disorder true \
            --disorder-ratio 0.3 \
            --max-disorder-us 5000 \
            --threads 4
        ;;
    
    "help"|*)
        echo "Usage: $0 [TEST_MODE]"
        echo ""
        echo "Available test modes:"
        echo "  baseline          - No disorder baseline test (100K events)"
        echo "  low-disorder      - 10% disorder, 2ms delay (100K events)"
        echo "  medium-disorder   - 30% disorder, 5ms delay (150K events)"
        echo "  high-disorder     - 50% disorder, 10ms delay (200K events)"
        echo "  extreme-disorder  - 70% disorder, 20ms delay (200K events)"
        echo "  large-scale       - 500K events with 30% disorder"
        echo "  stress-test       - Maximum stress test (600K events, high disorder)"
        echo "  quick             - Quick test with smaller dataset (50K events)"
        echo "  all               - Run all scenarios (default)"
        echo "  help              - Show this help"
        echo ""
        echo "Example:"
        echo "  $0 high-disorder"
        echo "  $0 all"
        ;;
esac

echo ""
echo -e "${BLUE}Test suite finished.${NC}"
