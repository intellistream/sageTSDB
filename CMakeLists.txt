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
    src/core/storage_engine.cpp
    src/core/lsm_tree.cpp
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

# Plugins library (optional, if PECJ is available)
option(BUILD_PLUGINS "Build plugin system" ON)
if(BUILD_PLUGINS)
    # Check for PECJ
    if(DEFINED PECJ_DIR)
        # Convert to absolute path if relative
        get_filename_component(PECJ_DIR_ABS "${PECJ_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(PECJ_INCLUDE_DIR "${PECJ_DIR_ABS}/include")
        message(STATUS "PECJ directory: ${PECJ_DIR_ABS}")
        message(STATUS "PECJ include directory: ${PECJ_INCLUDE_DIR}")
        
        if(EXISTS "${PECJ_INCLUDE_DIR}")
            add_library(sage_tsdb_plugins
                src/plugins/plugin_manager.cpp
                src/plugins/adapters/pecj_adapter.cpp
                src/plugins/adapters/fault_detection_adapter.cpp
            )
            
            target_include_directories(sage_tsdb_plugins
                PUBLIC
                    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                    $<INSTALL_INTERFACE:include>
                    ${PECJ_INCLUDE_DIR}
                PRIVATE
                    ${PROJECT_SOURCE_DIR}/src
            )
            
            target_link_libraries(sage_tsdb_plugins
                PUBLIC
                    sage_tsdb_core
            )
            
            message(STATUS "Plugin system enabled with PECJ support")
        else()
            message(WARNING "PECJ include directory not found: ${PECJ_INCLUDE_DIR}")
            message(WARNING "Building plugins without PECJ support")
        endif()
    else()
        message(STATUS "PECJ_DIR not set, skipping plugin build")
        message(STATUS "To enable plugins: cmake -DPECJ_DIR=/path/to/PECJ ..")
    endif()
endif()

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

# Install targets - Conditional installation for SKBUILD and standalone builds
if(DEFINED SKBUILD)
    # 判断是否是 editable install
    # SKBUILD_STATE 可能是 "editable" 或 "wheel" 或未定义
    # 如果未定义或不是 "wheel"，默认按 editable 处理
    set(_is_editable TRUE)
    if(DEFINED SKBUILD_STATE)
        if(SKBUILD_STATE STREQUAL "wheel")
            set(_is_editable FALSE)
        endif()
    endif()
    
    if(_is_editable)
        # Editable install: install to source tree directly
        set(_lib_install_dest "${CMAKE_CURRENT_SOURCE_DIR}/python")
        message(STATUS "sageTSDB: Editable install, lib -> ${_lib_install_dest}")
    else()
        # Wheel build: install to wheel platlib (same directory as Python extension)
        set(_lib_install_dest "${SKBUILD_PLATLIB_DIR}/sage/middleware/components/sage_tsdb/python")
        message(STATUS "sageTSDB: Wheel build, lib -> ${_lib_install_dest}")
    endif()
    
    # Python package build mode - install to same directory as Python extension
    install(TARGETS sage_tsdb_core sage_tsdb_algorithms
        LIBRARY DESTINATION ${_lib_install_dest}
        ARCHIVE DESTINATION ${_lib_install_dest}
        RUNTIME DESTINATION ${_lib_install_dest}
        COMPONENT python
    )
else()
    # Standalone C++ build mode - standard installation paths
    install(TARGETS sage_tsdb sage_tsdb_core sage_tsdb_algorithms
        EXPORT sageTSDBTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
    )

    # Export targets for standalone build
    install(EXPORT sageTSDBTargets
        FILE sageTSDBTargets.cmake
        NAMESPACE sageTSDB::
        DESTINATION lib/cmake/sageTSDB
    )

    # Install headers
    install(DIRECTORY include/sage_tsdb
        DESTINATION include
        FILES_MATCHING PATTERN "*.h"
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
endif()

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
message(STATUS "  Build plugins: ${BUILD_PLUGINS}")
if(TARGET sage_tsdb_plugins)
    message(STATUS "  PECJ support: YES (${PECJ_DIR})")
else()
    message(STATUS "  PECJ support: NO")
endif()
message(STATUS "  OpenMP support: ${ENABLE_OPENMP}")
message(STATUS "")

