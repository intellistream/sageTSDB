#!/bin/bash
# Benchmark different disorder scenarios and generate performance comparison

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_ROOT}/build"
DEMO_BIN="${BUILD_DIR}/examples/deep_integration_demo"
RESULTS_DIR="${BUILD_DIR}/benchmark_results"

# Color codes
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║      sageTSDB + PECJ: Disorder Benchmark Suite                ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULT_FILE="${RESULTS_DIR}/disorder_benchmark_${TIMESTAMP}.csv"
SUMMARY_FILE="${RESULTS_DIR}/disorder_benchmark_${TIMESTAMP}_summary.txt"

# Check if demo binary exists
if [ ! -f "$DEMO_BIN" ]; then
    echo -e "${RED}❌ Demo binary not found: $DEMO_BIN${NC}"
    exit 1
fi

# Find dataset files
PECJ_DATA_DIR=""
for dir in "${PROJECT_ROOT}/../../../PECJ/benchmark/datasets" \
           "${PROJECT_ROOT}/examples/datasets" \
           "/home/cdb/dameng/PECJ/benchmark/datasets"; do
    if [ -d "$dir" ] && [ -f "$dir/sTuple.csv" ]; then
        PECJ_DATA_DIR="$dir"
        break
    fi
done

if [ -z "$PECJ_DATA_DIR" ]; then
    echo -e "${RED}❌ Dataset files not found!${NC}"
    exit 1
fi

S_FILE="${PECJ_DATA_DIR}/sTuple.csv"
R_FILE="${PECJ_DATA_DIR}/rTuple.csv"
echo -e "${GREEN}✓ Using datasets from: $PECJ_DATA_DIR${NC}"
echo ""

# Initialize CSV file
echo "Scenario,Events,DisorderRatio,MaxDelayUs,TotalTimeMs,LoadThroughput,InsertThroughput,ComputeTimeMs,DisoreredEvents,LateArrivals,MaxDisorderMs,AvgDisorderMs,Windows,JoinResults" > "$RESULT_FILE"

# Function to run a benchmark and extract metrics
run_benchmark() {
    local scenario="$1"
    local events="$2"
    local disorder_ratio="$3"
    local max_delay="$4"
    
    echo -e "${YELLOW}Running: $scenario${NC}"
    echo "  Events: $events, Disorder: ${disorder_ratio}%, Max Delay: ${max_delay}us"
    
    # Run the demo and capture output
    local output=$($DEMO_BIN \
        --s-file "$S_FILE" \
        --r-file "$R_FILE" \
        --max-s $((events/2)) \
        --max-r $((events/2)) \
        --disorder true \
        --disorder-ratio "$disorder_ratio" \
        --max-disorder-us "$max_delay" \
        --threads 8 \
        --quiet 2>&1)
    
    # Extract metrics using grep and awk
    local total_time=$(echo "$output" | grep "Total Time" | awk '{print $4}')
    local load_throughput=$(echo "$output" | grep "Load Throughput" | awk '{print $4}')
    local insert_throughput=$(echo "$output" | grep "Insert Throughput" | awk '{print $4}')
    local compute_time=$(echo "$output" | grep "Computation Time" | awk '{print $4}')
    local disordered=$(echo "$output" | grep "Disordered Events" | awk '{print $4}')
    local late_arrivals=$(echo "$output" | grep "Late Arrivals" | awk '{print $4}')
    local max_disorder=$(echo "$output" | grep "Max Disorder Delay" | awk '{print $5}')
    local avg_disorder=$(echo "$output" | grep "Avg Disorder Delay" | awk '{print $5}')
    local windows=$(echo "$output" | grep "Windows Triggered" | awk '{print $4}')
    local joins=$(echo "$output" | grep "Join Results" | head -1 | awk '{print $4}')
    
    # Write to CSV
    echo "$scenario,$events,$disorder_ratio,$max_delay,$total_time,$load_throughput,$insert_throughput,$compute_time,$disordered,$late_arrivals,$max_disorder,$avg_disorder,$windows,$joins" >> "$RESULT_FILE"
    
    echo -e "${GREEN}  ✓ Completed: ${total_time}ms total, ${insert_throughput} events/s${NC}"
    echo ""
}

# Start benchmarking
echo -e "${BLUE}Starting benchmark suite...${NC}"
echo "Results will be saved to: $RESULT_FILE"
echo ""

# Test 1: Baseline (No disorder)
echo -e "${GREEN}=== Test 1: Baseline (No Disorder) ===${NC}"
$DEMO_BIN \
    --s-file "$S_FILE" \
    --r-file "$R_FILE" \
    --max-s 25000 \
    --max-r 25000 \
    --disorder false \
    --threads 8 > "${RESULTS_DIR}/baseline_${TIMESTAMP}.log" 2>&1

echo "Baseline completed. Output saved to baseline_${TIMESTAMP}.log"
echo ""

# Test 2-8: Varying disorder ratios (10%, 20%, 30%, 40%, 50%, 60%, 70%)
echo -e "${GREEN}=== Test Series: Varying Disorder Ratio ===${NC}"
for ratio in 0.1 0.2 0.3 0.4 0.5 0.6 0.7; do
    run_benchmark "Disorder-${ratio}" 50000 "$ratio" 5000
done

# Test 9-13: Varying delay (1ms, 2ms, 5ms, 10ms, 20ms) at 30% disorder
echo -e "${GREEN}=== Test Series: Varying Delay (30% disorder) ===${NC}"
for delay in 1000 2000 5000 10000 20000; do
    run_benchmark "Delay-${delay}us" 50000 0.3 "$delay"
done

# Test 14-16: Scaling tests (50K, 100K, 200K events) at 30% disorder, 5ms delay
echo -e "${GREEN}=== Test Series: Scaling Tests ===${NC}"
for events in 50000 100000 200000; do
    run_benchmark "Scale-${events}" "$events" 0.3 5000
done

# Generate summary
echo "" | tee "$SUMMARY_FILE"
echo "╔════════════════════════════════════════════════════════════════╗" | tee -a "$SUMMARY_FILE"
echo "║         Benchmark Summary - High Disorder Performance         ║" | tee -a "$SUMMARY_FILE"
echo "╚════════════════════════════════════════════════════════════════╝" | tee -a "$SUMMARY_FILE"
echo "" | tee -a "$SUMMARY_FILE"
echo "Timestamp: $TIMESTAMP" | tee -a "$SUMMARY_FILE"
echo "Total Tests: $(wc -l < "$RESULT_FILE" | awk '{print $1-1}')" | tee -a "$SUMMARY_FILE"
echo "" | tee -a "$SUMMARY_FILE"

# Display CSV content as table
echo "Results:" | tee -a "$SUMMARY_FILE"
column -t -s ',' "$RESULT_FILE" | tee -a "$SUMMARY_FILE"

echo "" | tee -a "$SUMMARY_FILE"
echo "Detailed logs saved in: $RESULTS_DIR" | tee -a "$SUMMARY_FILE"
echo "CSV results: $RESULT_FILE" | tee -a "$SUMMARY_FILE"
echo "" | tee -a "$SUMMARY_FILE"

# Find best and worst throughput
echo "Performance Highlights:" | tee -a "$SUMMARY_FILE"
echo "----------------------" | tee -a "$SUMMARY_FILE"

# Skip header and find max/min throughput
tail -n +2 "$RESULT_FILE" | sort -t',' -k7 -n | tail -1 | while IFS=',' read -r scenario events disorder delay total load insert compute dis late max avg win joins; do
    echo "  Best Insert Throughput: $insert events/s ($scenario)" | tee -a "$SUMMARY_FILE"
done

tail -n +2 "$RESULT_FILE" | sort -t',' -k7 -n | head -1 | while IFS=',' read -r scenario events disorder delay total load insert compute dis late max avg win joins; do
    echo "  Worst Insert Throughput: $insert events/s ($scenario)" | tee -a "$SUMMARY_FILE"
done

tail -n +2 "$RESULT_FILE" | sort -t',' -k5 -n | head -1 | while IFS=',' read -r scenario events disorder delay total load insert compute dis late max avg win joins; do
    echo "  Fastest Total Time: ${total}ms ($scenario)" | tee -a "$SUMMARY_FILE"
done

tail -n +2 "$RESULT_FILE" | sort -t',' -k5 -n | tail -1 | while IFS=',' read -r scenario events disorder delay total load insert compute dis late max avg win joins; do
    echo "  Slowest Total Time: ${total}ms ($scenario)" | tee -a "$SUMMARY_FILE"
done

echo "" | tee -a "$SUMMARY_FILE"
echo "╔════════════════════════════════════════════════════════════════╗" | tee -a "$SUMMARY_FILE"
echo "║                Benchmark Suite Completed!                     ║" | tee -a "$SUMMARY_FILE"
echo "╚════════════════════════════════════════════════════════════════╝" | tee -a "$SUMMARY_FILE"
