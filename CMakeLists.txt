cmake_minimum_required(VERSION 3.15)
project(sageTSDB VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_OPENMP "Enable OpenMP support" ON)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Find OpenMP
if(ENABLE_OPENMP)
    find_package(OpenMP)
    if(OpenMP_CXX_FOUND)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    endif()
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

# Core library
add_library(sage_tsdb_core
    src/core/time_series_data.cpp
    src/core/time_series_index.cpp
    src/core/time_series_db.cpp
    src/utils/config.cpp
)

target_include_directories(sage_tsdb_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

# Algorithms library
add_library(sage_tsdb_algorithms
    src/algorithms/stream_join.cpp
    src/algorithms/window_aggregator.cpp
)

target_include_directories(sage_tsdb_algorithms
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(sage_tsdb_algorithms
    PUBLIC
        sage_tsdb_core
)

# Main library (combines all)
add_library(sage_tsdb INTERFACE)
target_link_libraries(sage_tsdb
    INTERFACE
        sage_tsdb_core
        sage_tsdb_algorithms
)

# Link OpenMP if available
if(OpenMP_CXX_FOUND)
    target_link_libraries(sage_tsdb_core PUBLIC OpenMP::OpenMP_CXX)
endif()

# Install targets
install(TARGETS sage_tsdb sage_tsdb_core sage_tsdb_algorithms
    EXPORT sageTSDBTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Install headers
install(DIRECTORY include/sage_tsdb
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT sageTSDBTargets
    FILE sageTSDBTargets.cmake
    NAMESPACE sageTSDB::
    DESTINATION lib/cmake/sageTSDB
)

# Create config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/sageTSDBConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_file(cmake/sageTSDBConfig.cmake.in
    "${PROJECT_BINARY_DIR}/sageTSDBConfig.cmake"
    @ONLY
)

install(FILES
    "${PROJECT_BINARY_DIR}/sageTSDBConfig.cmake"
    "${PROJECT_BINARY_DIR}/sageTSDBConfigVersion.cmake"
    DESTINATION lib/cmake/sageTSDB
)

# Python bindings
if(BUILD_PYTHON_BINDINGS)
    add_subdirectory(python)
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
add_subdirectory(examples)

# Print configuration
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build Python bindings: ${BUILD_PYTHON_BINDINGS}")
message(STATUS "  Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  OpenMP support: ${ENABLE_OPENMP}")
message(STATUS "")
