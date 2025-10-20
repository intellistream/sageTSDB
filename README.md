# sageTSDB

**High-Performance Time Series Database with C++ Core**

sageTSDB is a high-performance time series database designed for streaming data processing with support for out-of-order data, window-based operations, and pluggable algorithms.

## 🌟 Features

- **Efficient Time Series Storage**: Optimized data structures for time series indexing
- **Out-of-Order Data Handling**: Automatic buffering and watermarking for late data
- **Pluggable Algorithms**: Extensible architecture for custom stream processing algorithms
- **Window Operations**: Support for tumbling, sliding, and session windows
- **Stream Join**: Window-based join for multiple time series streams
- **Python Bindings**: Easy-to-use Python API via pybind11

## 🏗️ Architecture

```
sageTSDB/
├── include/
│   └── sage_tsdb/
│       ├── core/
│       │   ├── time_series_data.h      # Data structures
│       │   ├── time_series_index.h     # Indexing
│       │   └── time_series_db.h        # Core database
│       ├── algorithms/
│       │   ├── algorithm_base.h        # Algorithm interface
│       │   ├── stream_join.h           # Stream join algorithm
│       │   └── window_aggregator.h     # Window aggregation
│       └── utils/
│           ├── config.h                # Configuration
│           └── common.h                # Common utilities
├── src/
│   ├── core/
│   ├── algorithms/
│   └── utils/
├── python/
│   └── bindings.cpp                    # pybind11 bindings
├── tests/
│   └── cpp/
└── CMakeLists.txt
```

## 📦 Building

### Prerequisites

- C++17 compatible compiler (GCC 8+, Clang 7+, MSVC 2019+)
- CMake 3.15 or higher
- Python 3.8+ (for Python bindings)
- pybind11

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/intellistream/sageTSDB.git
cd sageTSDB

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j$(nproc)

# Run tests
ctest

# Install (optional)
sudo make install
```

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
cd build
ctest -V

# Run specific test
./tests/test_time_series_db
./tests/test_stream_join
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

- [API Reference](docs/API.md)
- [Algorithm Guide](docs/ALGORITHMS.md)
- [Performance Tuning](docs/PERFORMANCE.md)
- [Python Bindings](docs/PYTHON_BINDINGS.md)

## 🤝 Contributing

Contributions are welcome! Please read our [Contributing Guide](CONTRIBUTING.md) for details.

## 📄 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## 🔗 Links

- [SAGE Project](https://github.com/intellistream/SAGE)
- [Documentation](https://sage-docs.example.com)
- [Issue Tracker](https://github.com/intellistream/sageTSDB/issues)

## 📮 Contact

For questions and support:
- GitHub Issues: https://github.com/intellistream/sageTSDB/issues
- Email: shuhao_zhang@hust.edu.cn
