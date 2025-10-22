cmake_minimum_required(VERSION 3.15)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Test utilities
add_library(test_utils INTERFACE)
target_include_directories(test_utils INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Core tests
add_executable(test_time_series_data
  test_time_series_data.cpp
)
target_link_libraries(test_time_series_data
  PRIVATE
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

add_executable(test_time_series_index
  test_time_series_index.cpp
)
target_link_libraries(test_time_series_index
  PRIVATE
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

add_executable(test_time_series_db
  test_time_series_db.cpp
)
target_link_libraries(test_time_series_db
  PRIVATE
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

# Algorithm tests
add_executable(test_stream_join
  test_stream_join.cpp
)
target_link_libraries(test_stream_join
  PRIVATE
    sage_tsdb_algorithms
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

add_executable(test_window_aggregator
  test_window_aggregator.cpp
)
target_link_libraries(test_window_aggregator
  PRIVATE
    sage_tsdb_algorithms
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

add_executable(test_storage_engine
  test_storage_engine.cpp
)
target_link_libraries(test_storage_engine
  PRIVATE
    sage_tsdb_core
    GTest::gtest_main
    test_utils
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(test_time_series_data)
gtest_discover_tests(test_time_series_index)
gtest_discover_tests(test_time_series_db)
gtest_discover_tests(test_stream_join)
gtest_discover_tests(test_window_aggregator)
gtest_discover_tests(test_storage_engine)
