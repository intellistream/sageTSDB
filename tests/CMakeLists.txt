cmake_minimum_required(VERSION 3.15)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Ensure GoogleTest is compiled with the same C++ standard and ABI as the main project
set(CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD} CACHE INTERNAL "")
set(CMAKE_CXX_STANDARD_REQUIRED ON CACHE INTERNAL "")

# Force old ABI for compatibility with PyTorch/PECJ (they use old ABI)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

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

# Plugin tests (only if plugins are built)
if(TARGET sage_tsdb_plugins)
    add_executable(test_pecj_plugin
      test_pecj_plugin.cpp
    )
    target_link_libraries(test_pecj_plugin
      PRIVATE
        sage_tsdb_plugins
        sage_tsdb_core
        GTest::gtest_main
        test_utils
    )
    
    add_executable(test_fault_detection_plugin
      test_fault_detection_plugin.cpp
    )
    target_link_libraries(test_fault_detection_plugin
      PRIVATE
        sage_tsdb_plugins
        sage_tsdb_core
        GTest::gtest_main
        test_utils
    )
    
    add_executable(test_resource_manager
      test_resource_manager.cpp
    )
    target_link_libraries(test_resource_manager
      PRIVATE
        sage_tsdb_plugins
        sage_tsdb_core
        GTest::gtest_main
        test_utils
    )
    
    # Integrated mode test (uses ResourceManager with PECJ)
    add_executable(test_integrated_mode
      test_integrated_mode.cpp
    )
    target_link_libraries(test_integrated_mode
      PRIVATE
        sage_tsdb_plugins
        sage_tsdb_core
    )
    
    message(STATUS "Building plugin tests")
endif()

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(test_time_series_data)
gtest_discover_tests(test_time_series_index)
gtest_discover_tests(test_time_series_db)
gtest_discover_tests(test_stream_join)
gtest_discover_tests(test_window_aggregator)
gtest_discover_tests(test_storage_engine)

if(TARGET test_pecj_plugin)
    gtest_discover_tests(test_pecj_plugin)
endif()

if(TARGET test_fault_detection_plugin)
    gtest_discover_tests(test_fault_detection_plugin)
endif()

if(TARGET test_resource_manager)
    gtest_discover_tests(test_resource_manager)
endif()

# Note: test_integrated_mode is not registered with CTest as it's a manual verification tool

