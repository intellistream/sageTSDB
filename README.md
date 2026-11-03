# sageTSDB

**High-Performance Time Series Database with C++ Core**

[![PyPI version](https://badge.fury.io/py/isage-tsdb.svg)](https://pypi.org/project/isage-tsdb/)
[![Python 3.10+](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/downloads/)
[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

sageTSDB is a high-performance time series database designed for streaming data processing with support for out-of-order data, window-based operations, and pluggable algorithms.

**Repository Owner**: Debin Chen (GitHub: [@pluviophile-chen](https://github.com/pluviophile-chen))

## 🚀 Quick Install

```bash
pip install isage-tsdb
```

**Requirements**: Ubuntu 22.04+ (GLIBC 2.35+) or equivalent Linux distribution.

## 🌟 Features

- **Efficient Time Series Storage**: Optimized data structures for time series indexing
- **Out-of-Order Data Handling**: Automatic buffering and watermarking for late data
- **Pluggable Algorithms**: Extensible architecture for custom stream processing algorithms
- **Window Operations**: Support for tumbling, sliding, and session windows
- **Stream Join**: Window-based join for multiple time series streams
- **Python Bindings**: Easy-to-use Python API via pybind11

## 🏗️ Project Structure

```
sageTSDB/
├── include/sage_tsdb/   # Public C++ headers and C ABI
├── src/                # core/, compute/, algorithms/, plugins/, utils/, c_api/
├── sage_tsdb/          # Python package, bindings and intelligence adapters
├── tools/              # PECJ and SRTFD command-line programs
├── cmake/              # Package exports and delivery build settings
├── tests/              # C++ and Python regression tests
├── examples/           # basic/, integration/, benchmarks/, python/, task_demos/
├── docs/               # Architecture, interfaces, delivery and research requirements
├── scripts/            # build/, demo/, test/, release/
├── docker/             # manylinux build image
├── hooks/              # Optional development Git hooks
└── CMakeLists.txt      # Main C++ build and install entrypoint
```

- Start with [QUICKSTART.md](QUICKSTART.md) or the [documentation index](docs/README.md).
- Use [scripts/build/build.sh](scripts/build/build.sh) for C++ builds and tests.
- See [component delivery](docs/compute/PECJ_COMPUTE_COMPONENT_DELIVERY.md),
  [upstream network adapters](docs/compute/RESEARCH_ENGINE.md), and
  [task demos](examples/task_demos/README.md) for standalone delivery workflows.
- Generated builds belong in `build/`, packages in `dist/`, and demo results in
  `examples/outputs/`. These are local artifacts; edit source files outside `dist/`
  and rebuild packages to update a delivery.

## 📦 Quick Start (Python)

### Installation

```bash
# Install from PyPI (recommended)
pip install isage-tsdb

# Verify installation
python -c "import sage_tsdb; print(sage_tsdb.__version__)"
```

**System Requirements**: 
- Ubuntu 22.04+ (GLIBC 2.35+) or equivalent
- Python 3.10+

### Basic Usage

```python
import sage_tsdb

# Create database
db = sage_tsdb.TimeSeriesDB()

# Insert data
db.add(
    timestamp=1000000,  # microseconds
    value=23.5,
    tags={"sensor": "temp_01", "location": "room_a"},
    fields={"unit": "celsius"}
)

# Query data
data = db.query(start=0, end=3000000)
print(f"Found {len(data)} data points")
```

For more examples, see [Python Examples](#python-usage) below.

## 📦 Building from Source

### Prerequisites

- C++20 compatible compiler (GCC 11+, Clang 12+)
- CMake 3.15 or higher
- Python 3.10+ and pip (only for PECJ/PyTorch integration and Python bindings)

### Build Instructions (core only)

Builds the time series core, built-in algorithms, and the SRTFD `statistical`
diagnosis engine — no PECJ/PyTorch required.

```bash
# Clone the repository
git clone https://github.com/intellistream/sageTSDB.git
cd sageTSDB

# Configure and build (out-of-source)
cmake -B build -S . -DBUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure
```

### Build with PECJ + SRTFD integration and run benchmarks

To enable the PECJ out-of-order stream-join engine and run the benchmarks, you
also clone and build PECJ (which links PyTorch). See **[QUICKSTART.md](QUICKSTART.md)**
for the full end-to-end walkthrough. In short:

```bash
# 1. Alongside sageTSDB, clone PECJ (join engine) and SRTFD (diagnosis)
git clone https://github.com/intellistream/PECJ.git
git clone https://github.com/intellistream/SRTFD.git

# 2. Install PyTorch (PECJ dependency) and build PECJ
pip3 install torch==1.13.0 --index-url https://download.pytorch.org/whl/cpu
cd PECJ && mkdir -p build && cd build
cmake -DCMAKE_PREFIX_PATH=$(python3 -c 'import torch; print(torch.utils.cmake_prefix_path)') ..
make -j$(nproc) && cd ../..

# 3. Configure sageTSDB with PECJ deep integration
cd sageTSDB
cmake -B build -S . \
    -DSAGE_TSDB_ENABLE_PECJ=ON -DPECJ_MODE=INTEGRATED \
    -DPECJ_FULL_INTEGRATION=ON -DPECJ_DIR=../PECJ \
    -DSAGE_TSDB_ENABLE_SRTFD=ON -DBUILD_TESTS=ON
cmake --build build -j$(nproc)

# 4. Point the loader at PECJ + Torch libs, then run a benchmark
export LD_LIBRARY_PATH=../PECJ/build:\
$(python3 -c 'import torch,os;print(os.path.dirname(torch.__file__))')/lib:$LD_LIBRARY_PATH
./build/examples/performance_benchmark \
    --s-file examples/datasets/sTuple.csv \
    --r-file examples/datasets/rTuple.csv \
    --output /tmp/benchmark_results.csv
```

> Enterprise database (达梦 DM) integration: see
> [docs/STORAGE_BACKEND_CONTRACT.md](docs/STORAGE_BACKEND_CONTRACT.md).

### Build Python Bindings

```bash
# From build directory
cmake -DBUILD_PYTHON_BINDINGS=ON ..
make -j$(nproc)

# Install Python package
pip install .
```

## 🚀 Quick Start

### C++ API

```cpp
#include <sage_tsdb/core/time_series_db.h>
#include <sage_tsdb/algorithms/stream_join.h>

using namespace sage_tsdb;

int main() {
    // Create database
    TimeSeriesDB db;
    
    // Add data
    TimeSeriesData data;
    data.timestamp = 1234567890000;
    data.value = 42.5;
    data.tags["sensor"] = "temp_01";
    
    db.add(data);
    
    // Query data
    TimeRange range{1234567890000, 1234567900000};
    auto results = db.query(range);
    
    // Use algorithms
    StreamJoin join(5000); // 5-second window
    auto joined = join.process(left_stream, right_stream);
    
    return 0;
}
```

### Python API

```python
import sage_tsdb

# Create database
db = sage_tsdb.TimeSeriesDB()

# Add data
db.add(timestamp=1234567890000, value=42.5, 
       tags={"sensor": "temp_01"})

# Query data
results = db.query(start_time=1234567890000,
                  end_time=1234567900000)

# Stream join
join = sage_tsdb.StreamJoin(window_size=5000)
joined = join.process(left_stream, right_stream)
```

## 🔌 Pluggable Algorithms

### Implementing Custom Algorithms

```cpp
#include <sage_tsdb/algorithms/algorithm_base.h>

class MyAlgorithm : public TimeSeriesAlgorithm {
public:
    MyAlgorithm(const AlgorithmConfig& config) 
        : TimeSeriesAlgorithm(config) {}
    
    std::vector<TimeSeriesData> process(
        const std::vector<TimeSeriesData>& input) override {
        // Your algorithm implementation
        return output;
    }
};

// Register algorithm
REGISTER_ALGORITHM("my_algorithm", MyAlgorithm);
```

## 🧪 Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run a specific test
./build/tests/test_time_series_db
./build/tests/test_storage_backend_contract   # storage backend contract + stsb1 codec
./build/tests/test_srtfd_compute_engine       # SRTFD diagnosis (statistical backend)
```

## 📊 Performance

Benchmarks on typical hardware (Intel i7, 16GB RAM):

| Operation | Throughput | Latency |
|-----------|-----------|---------|
| Single insert | 1M ops/sec | < 1 μs |
| Batch insert (1000) | 5M ops/sec | < 200 ns/op |
| Query (1000 results) | 500K queries/sec | 2 μs |
| Stream join | 300K pairs/sec | 3 μs |
| Window aggregation | 800K windows/sec | 1.2 μs |

## 🔗 Integration with SAGE

This library is designed to be used as a submodule in the SAGE project:

```bash
# In SAGE repository
git submodule add https://github.com/intellistream/sageTSDB.git \
    packages/sage-middleware/src/sage/middleware/components/sage_tsdb/sageTSDB

git submodule update --init --recursive
```

## 📚 Documentation

- [Architecture and interfaces](docs/DESIGN_DOC_SAGETSDB_PECJ.md)
- [Algorithm Guide](docs/algorithms/README.md)
- [Performance Benchmarks](docs/compute/PECJ_BENCHMARK_README.md)
- [Python Examples](examples/python/README.md)

## 🤝 Contributing

Contributions are welcome. Include a clear description and relevant test results in your pull request.

## 📄 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## 🔗 Links

- [SAGE Project](https://github.com/intellistream/SAGE)
- [Documentation](docs/README.md)
- [Issue Tracker](https://github.com/intellistream/sageTSDB/issues)

## 📮 Contact

For questions and support:
- GitHub Issues: https://github.com/intellistream/sageTSDB/issues
- Owner: Debin Chen (@pluviophile-chen)
- GitHub: [pluviophile-chen](https://github.com/pluviophile-chen)
