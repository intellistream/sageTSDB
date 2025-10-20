# sageTSDB Tests

This directory contains comprehensive unit tests for sageTSDB using Google Test framework.

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
mkdir -p build
cd build

# Configure with tests enabled
cmake .. -G Ninja -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build .
```

### Run All Tests
```bash
# From build directory
ctest --output-on-failure --verbose
```

### Run Specific Test
```bash
# From build directory
./test_time_series_data
./test_time_series_index
./test_time_series_db
./test_stream_join
./test_window_aggregator
```

### Run Tests with Filtering
```bash
# Run only tests matching pattern
./test_time_series_index --gtest_filter="*Query*"

# List all tests
./test_time_series_index --gtest_list_tests
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
cd build
rm -rf *
cmake .. -DBUILD_TESTS=ON
cmake --build .
```

## Test Statistics

Run `ctest` to see summary:
```
Test project /path/to/build
    Start 1: test_time_series_data
1/5 Test #1: test_time_series_data ............   Passed    0.01 sec
    Start 2: test_time_series_index
2/5 Test #2: test_time_series_index ...........   Passed    0.02 sec
    Start 3: test_time_series_db
3/5 Test #3: test_time_series_db ..............   Passed    0.01 sec
    Start 4: test_stream_join
4/5 Test #4: test_stream_join .................   Passed    0.01 sec
    Start 5: test_window_aggregator
5/5 Test #5: test_window_aggregator ...........   Passed    0.02 sec

100% tests passed, 0 tests failed out of 5
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
