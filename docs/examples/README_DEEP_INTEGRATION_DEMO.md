  # Deep Integration Demo - Real Dataset Usage Guide

## Overview

The improved `deep_integration_demo` now uses **real PECJ benchmark datasets** instead of synthetic data, providing a more realistic demonstration of the sageTSDB + PECJ integration.

## What's New (v4.0)

### ✨ Key Improvements

1. **Real Dataset Support**
   - Loads PECJ CSV benchmark datasets (sTuple.csv, rTuple.csv)
   - Format: `key,value,eventTime,arrivalTime`
   - Processes up to 120K+ real events

2. **Multiple Windows**
   - Configurable sliding window parameters
   - Triggers multiple window computations based on data time range
   - Realistic window-based stream processing

3. **Enhanced Statistics**
   - Data loading time
   - Insertion throughput
   - Per-window computation time
   - Overall performance metrics

4. **Flexible Configuration**
   - Command-line parameters for all settings
   - Customizable window length and slide
   - Adjustable event limits per stream

## Quick Start

### 1. Build the Demo

```bash
cd /path/to/sageTSDB/build
cmake -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED ..
make deep_integration_demo -j4
```

### 2. Run with Default Settings

**Note**: By default, the demo assumes CSV timestamps are in **milliseconds** and converts them to microseconds internally.

```bash
cd build/examples
./deep_integration_demo \
  --s-file /home/cdb/dameng/PECJ/benchmark/datasets/sTuple.csv \
  --r-file /home/cdb/dameng/PECJ/benchmark/datasets/rTuple.csv
```

### 3. Example Output

```
[Configuration]
  Stream S File         : .../sTuple.csv
  Stream R File         : .../rTuple.csv
  Max Events per Stream : S=60000, R=60000
  CSV Time Unit         : milliseconds (ms)
  Window Length         : 1000 ms
  Slide Length          : 500 ms

[Data Loading]
  Stream S Loaded       : 60000 events
  Stream R Loaded       : 60000 events
  Load Time             : 97 ms
  Data Time Span        : 992 ms

[Window Computation]
  Windows Triggered     : 10
  Join Results          : 191,012
  Computation Time      : 95 ms

[Overall Performance]
  Total Time            : 511 ms
  Overall Throughput    : 234,834 events/s
```

## Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--s-file PATH` | Path to stream S CSV file | `../../../PECJ/benchmark/datasets/sTuple.csv` |
| `--r-file PATH` | Path to stream R CSV file | `../../../PECJ/benchmark/datasets/rTuple.csv` |
| `--max-s N` | Max events from stream S | 60000 |
| `--max-r N` | Max events from stream R | 60000 |
| `--window-us N` | Window length (microseconds) | 10000 (10ms) |
| `--slide-us N` | Slide length (microseconds) | 5000 (5ms) |
| `--threads N` | Max computation threads | 4 |
| `--time-unit UNIT` | CSV time unit: 'ms' or 'us' | ms (milliseconds) |
| `--quiet` | Reduce output verbosity | false |
| `--help` | Show help message | - |

## Example Scenarios

### Scenario 1: High-Frequency Windows (More Windows)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --window-us 500000 \
  --slide-us 100000
```

**Result**: 10 windows, 191K+ joins (with 1 second data)

### Scenario 2: Large Window (Fewer Windows, More Joins per Window)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --window-us 2000000 \
  --slide-us 1000000
```

**Result**: 1-2 windows, higher join count per window (2s windows)

### Scenario 3: Limited Data (Faster Testing)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --max-s 10000 \
  --max-r 10000
```

**Result**: Quick test with 20K events

### Scenario 4: Different Time Unit (Microseconds)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --time-unit us \
  --window-us 5000 \
  --slide-us 500
```

**Result**: Treats CSV timestamps as microseconds (for datasets already in us)

### Scenario 5: More Threads (Better Performance)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --threads 8
```

**Result**: Improved computation throughput

## Architecture Highlights

### Data Flow

```
PECJ CSV Files
    ↓
CSVDataLoader (examples/csv_data_loader.h)
    ↓
Sort by arrival_time
    ↓
sageTSDB Tables (stream_s, stream_r)
    ↓
PECJ Compute Engine (stateless)
    ↓
Join Results → sageTSDB (join_results table)
```

### Key Components

1. **CSVDataLoader** (`examples/csv_data_loader.h`)
   - Parses PECJ CSV format
   - Provides data statistics
   - Converts to TimeSeriesData

2. **Deep Integration Demo** (`examples/deep_integration_demo.cpp`)
   - Loads real datasets
   - Sorts by arrival time (realistic stream simulation)
   - Triggers multiple sliding windows
   - Collects comprehensive statistics

3. **PECJ Compute Engine** (`src/compute/pecj_compute_engine.cpp`)
   - Stateless computation
   - Reads from sageTSDB tables
   - Executes window joins
   - Writes results back to sageTSDB

## Performance Considerations

### CSV Time Unit Handling

**Important**: PECJ CSV files typically use **milliseconds** for timestamps, but sageTSDB and PECJ internally work with **microseconds**.

The demo handles this automatically:
- **Default**: `--time-unit ms` → converts CSV milliseconds to microseconds (×1000)
- **Optional**: `--time-unit us` → treats CSV as already in microseconds (×1)

### Factors Affecting Window Count

The number of windows triggered depends on:
- **Data time span**: Time range in the dataset (e.g., 992ms for default sTuple.csv with ms unit)
- **Slide length**: Smaller slide → more windows
- **Formula**: `num_windows = (data_time_span / slide_len) + 1`

### Typical Results

With default PECJ datasets (milliseconds unit):
- **Data time span**: ~992ms (after conversion to us)
- **Window 1000ms, Slide 500ms**: 2 windows
- **Window 500ms, Slide 100ms**: 10 windows
- **Window 100ms, Slide 50ms**: 20 windows

### Join Result Count

Depends on:
- Key overlap between streams
- Window size (larger = more tuples in window)
- Data cardinality

## Comparison: Old vs New

| Aspect | v3.0 (Synthetic) | v4.0 (Real Dataset) |
|--------|------------------|---------------------|
| Data Source | Random generator | PECJ CSV files |
| Events | 20K fixed | Up to 120K+ |
| Windows | 1 (always) | 1-1000 (configurable) |
| Join Results | ~126K | 26K-191K+ (realistic) |
| Time Span | Artificial (~1ms) | Real benchmark (~992ms) |
| Time Unit | Fixed (us) | Configurable (ms/us) |
| Realism | Low | High |

## Troubleshooting

### Problem: "Failed to open file"

**Solution**: Use absolute paths
```bash
--s-file /absolute/path/to/sTuple.csv
```

### Problem: Only 1 window triggered

**Explanation**: This likely means you're using `--time-unit us` when the CSV is actually in milliseconds, resulting in a very short time span (~1ms).

**Solutions**:
1. Use correct time unit: `--time-unit ms` (default)
2. Reduce slide length: `--slide-us 100000` (100ms)
3. Check your CSV timestamps - are they in ms or us?

### Problem: Time span seems too large/small

**Solution**: Verify CSV time unit
- If time span shows as ~1ms but you have 1-second data → use `--time-unit ms`
- If time span shows as ~1000s but you have 1-second data → use `--time-unit us`

### Problem: No join results in final query

**Note**: This is expected in current version. Join results are printed during window computation but not yet persisted to result table.

## Next Steps

1. **Try Different Datasets**
   ```bash
   --s-file /path/to/PECJ/benchmark/datasets/1000ms_1tLowDelayData.csv
   ```

2. **Adjust Window Parameters**
   - Experiment with different window/slide combinations
   - Observe trade-off between window count and join results

3. **Analyze Performance**
   - Compare throughput with different thread counts
   - Measure impact of data size on performance

4. **Check Persisted Data**
   ```bash
   ls -lh build/sage_tsdb_data/lsm/
   ```

## Further Reading

- [PECJ Integration Documentation](../docs/PECJ_DEEP_INTEGRATION_SUMMARY.md)
- [Table Design](../docs/TABLE_DESIGN_IMPLEMENTATION.md)
- [Resource Manager Guide](../docs/RESOURCE_MANAGER_GUIDE.md)
- [PECJ Compute Engine](../docs/PECJ_COMPUTE_ENGINE_IMPLEMENTATION.md)

## Summary

The improved demo now provides a **realistic demonstration** of:
- ✅ Real PECJ benchmark data loading
- ✅ Multiple sliding window computations
- ✅ High-throughput stream processing (~240K events/s)
- ✅ Database-centric architecture (all data in sageTSDB)
- ✅ Stateless PECJ compute engine
- ✅ Comprehensive performance statistics

This makes the demo much more suitable for **benchmarking**, **testing**, and **demonstrating** the sageTSDB + PECJ integration capabilities!
