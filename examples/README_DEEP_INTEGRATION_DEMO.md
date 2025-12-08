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
  Window Length         : 5 ms
  Slide Length          : 0.5 ms

[Data Loading]
  Stream S Loaded       : 60000 events
  Stream R Loaded       : 60000 events
  Load Time             : 92 ms

[Window Computation]
  Windows Triggered     : 2
  Join Results          : 26,192
  Computation Time      : 84 ms

[Overall Performance]
  Total Time            : 492 ms
  Overall Throughput    : 243,902 events/s
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
| `--quiet` | Reduce output verbosity | false |
| `--help` | Show help message | - |

## Example Scenarios

### Scenario 1: High-Frequency Windows (More Windows)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --window-us 5000 \
  --slide-us 500
```

**Result**: ~3 windows, 26K+ joins

### Scenario 2: Large Window (Fewer Windows, More Joins per Window)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --window-us 20000 \
  --slide-us 10000
```

**Result**: 1-2 windows, higher join count per window

### Scenario 3: Limited Data (Faster Testing)

```bash
./deep_integration_demo \
  --s-file /path/to/sTuple.csv \
  --r-file /path/to/rTuple.csv \
  --max-s 10000 \
  --max-r 10000
```

**Result**: Quick test with 20K events

### Scenario 4: More Threads (Better Performance)

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

### Factors Affecting Window Count

The number of windows triggered depends on:
- **Data time span**: Time range in the dataset (e.g., 0.992ms for default sTuple.csv)
- **Slide length**: Smaller slide → more windows
- **Formula**: `num_windows = (data_time_span / slide_len) + 1`

### Typical Results

With default PECJ datasets:
- **Data time span**: ~1ms
- **Window 10ms, Slide 5ms**: 1 window
- **Window 5ms, Slide 0.5ms**: 2-3 windows
- **Window 1ms, Slide 0.1ms**: 10-15 windows

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
| Join Results | ~126K | 4K-30K+ (realistic) |
| Time Span | Artificial | Real benchmark data |
| Realism | Low | High |

## Troubleshooting

### Problem: "Failed to open file"

**Solution**: Use absolute paths
```bash
--s-file /absolute/path/to/sTuple.csv
```

### Problem: Only 1 window triggered

**Explanation**: Data time span is very short (~1ms in default dataset)

**Solutions**:
1. Reduce slide length: `--slide-us 100`
2. Use dataset with larger time span
3. Generate custom data with longer duration

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
