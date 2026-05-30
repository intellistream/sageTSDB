# sageTSDB Tests

This directory contains the active C++ and Python tests for sageTSDB. C++ tests use GoogleTest/CTest; Python tests use pytest.

## Test Coverage

### Core Tests
- **test_time_series_data.cpp**: Tests for core data structures
  - TimeSeriesData construction and manipulation
  - TimeRange operations
  - AggregationType conversions
  - QueryConfig functionality

- **test_time_series_index.cpp**: Tests for time series indexing
  - Adding single and multiple data points
  - Time range queries
  - Tag-based filtering
  - Query limits
  - Out-of-order inserts
  - Concurrent read safety
  - Statistics tracking

- **test_time_series_db.cpp**: Tests for main database interface
  - Add and query operations
  - Single and multiple tag filters
  - Database clearing
  - Large dataset handling
  - Empty database queries

### Algorithm Tests
- **test_stream_join.cpp**: Tests for out-of-order stream join
  - Basic join operations
  - Time offset handling
  - Out-of-order data processing
  - Watermark progression
  - Empty stream handling
  - Join statistics

- **test_window_aggregator.cpp**: Tests for window-based aggregations
  - All aggregation types (SUM, AVG, MIN, MAX, COUNT, STDDEV)
  - Tumbling window processing
  - Sliding window processing
  - Window alignment
  - Empty data handling
  - Multiple batch processing

### Storage and Table Tests
- **test_storage_engine.cpp**: Persistence, checkpoint, vector value, and large dataset tests.
- **test_table_design.cpp**: StreamTable, JoinResultTable, TableManager, and end-to-end table workflow tests.

### Compute Tests
- **test_srtfd_compute_engine.cpp**: SRTFD stateless diagnosis engine tests.
  - Initialization and result table creation
  - Fail-fast unsupported backend handling
  - Empty windows
  - Feature dimension validation
  - Diagnosis result writeback and metrics

- **test_pecj_compute_engine.cpp**: PECJ compute engine tests, built only when `SAGE_TSDB_ENABLE_PECJ=ON` and `PECJ_MODE=INTEGRATED`.
- **test_window_scheduler_simple.cpp**: Runtime-safe WindowScheduler tests for the integrated PECJ build.

### Plugin Tests
- **test_pecj_plugin.cpp**: PECJ plugin adapter lifecycle and feed/process behavior.
- **test_fault_detection_plugin.cpp**: Fault detection plugin adapter behavior.
- **test_resource_manager.cpp**: ResourceManager allocation, task, usage, limits, and release behavior.
- **test_integrated_mode.cpp**: Manual integrated-mode verification tool; it is built but intentionally not registered in CTest.

### Python Tests
- **test_smoke.py**: package import and top-level symbol smoke tests.
- **test_python_layer.py**: Python fallback core API tests.
- **test_algorithms.py**: Python out-of-order join and window aggregation tests.
- **test_service.py**: Python service wrapper tests.

## Building and Running Tests

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake ninja-build libfmt-dev libspdlog-dev

# macOS
brew install cmake ninja fmt spdlog
```

### Build Tests
```bash
# From sageTSDB root directory
cmake -B build_test_default -S . \
  -DBUILD_TESTS=ON \
  -DSAGE_TSDB_ENABLE_SRTFD=ON \
  -DSAGE_TSDB_ENABLE_PECJ=OFF \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build_test_default -j$(nproc)
```

### Run All Tests
```bash
# From sageTSDB root directory
ctest --test-dir build_test_default --output-on-failure
```

### Run Specific Test
```bash
./build_test_default/tests/test_time_series_data
./build_test_default/tests/test_time_series_index
./build_test_default/tests/test_time_series_db
./build_test_default/tests/test_stream_join
./build_test_default/tests/test_window_aggregator
./build_test_default/tests/test_srtfd_compute_engine
```

### Run Tests with Filtering
```bash
# Run only tests matching pattern
./build_test_default/tests/test_time_series_index --gtest_filter="*Query*"

# List all tests
./build_test_default/tests/test_srtfd_compute_engine --gtest_list_tests
```

### Python Tests
```bash
python3 -m pip install 'pytest>=7.0.0' 'pytest-cov>=4.0.0'
python3 -m pytest tests -q
```

### PECJ Integrated Tests
PECJ integrated tests require a local PECJ checkout and its dependencies:

```bash
cmake -B build_test_pecj -S . \
  -DBUILD_TESTS=ON \
  -DSAGE_TSDB_ENABLE_PECJ=ON \
  -DPECJ_MODE=INTEGRATED \
  -DPECJ_DIR=/path/to/PECJ \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build_test_pecj -j$(nproc)
ctest --test-dir build_test_pecj --output-on-failure
```

## Test Configuration

### Debug Build (for development)
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
```

### Release Build (for performance)
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
```

### With Coverage
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON \
  -DCMAKE_CXX_FLAGS="--coverage -fprofile-arcs -ftest-coverage"
cmake --build .
ctest
lcov --capture --directory . --output-file coverage.info
lcov --list coverage.info
```

## CI/CD Integration

Tests are automatically run in CI/CD pipeline on:
- Push to `main` or `main-dev` branches
- Pull requests
- Manual workflow dispatch

CI tests run on:
- Ubuntu (latest) with GCC
- macOS (latest) with Clang
- Both Debug and Release builds

## Test Results

Test results are uploaded as artifacts and can be viewed in GitHub Actions:
- Test outputs and logs
- Coverage reports (on Ubuntu Debug builds)
- Static analysis results

## Adding New Tests

1. Create new test file in `tests/` directory:
```cpp
#include <gtest/gtest.h>
#include "sage_tsdb/your_component.h"

class YourComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code
    }
    
    void TearDown() override {
        // Cleanup code
    }
};

TEST_F(YourComponentTest, TestName) {
    // Your test code
    EXPECT_EQ(actual, expected);
}
```

2. Add to `tests/CMakeLists.txt`:
```cmake
add_executable(test_your_component
  test_your_component.cpp
)
target_link_libraries(test_your_component
  PRIVATE
    sage_tsdb_core  # or sage_tsdb_algorithms
    GTest::gtest_main
    test_utils
)
gtest_discover_tests(test_your_component)
```

3. Build and run:
```bash
cd build
cmake --build .
./test_your_component
```

## Troubleshooting

### Issue: Cannot find headers
```bash
# Make sure you're building from build directory
cd build
cmake .. -DBUILD_TESTS=ON
```

### Issue: GoogleTest not found
GoogleTest is automatically fetched via CMake FetchContent. Ensure you have internet connection during first build.

### Issue: Tests fail on macOS
Some tests may behave differently on macOS due to different standard library implementations. Check CI results for platform-specific issues.

### Issue: Compilation errors
```bash
# Clean and rebuild
rm -rf build_test_default
cmake -B build_test_default -S . -DBUILD_TESTS=ON
cmake --build build_test_default
```

## Test Statistics

Run `ctest` to see summary:
```
100% tests passed, 0 tests failed
```

## Contributing

When contributing new features:
1. Write tests first (TDD approach)
2. Ensure all existing tests pass
3. Add tests for edge cases
4. Run static analysis tools
5. Check code coverage

## References

- [Google Test Documentation](https://google.github.io/googletest/)
- [CMake Testing](https://cmake.org/cmake/help/latest/manual/ctest.1.html)
- [sageTSDB Main README](../README.md)
