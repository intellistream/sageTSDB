cmake_minimum_required(VERSION 3.15)
project(sageTSDB VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard (C++20 required for PECJ integration)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force old ABI for compatibility with PyTorch/PECJ (they use old ABI)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

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
    src/core/resource_manager.cpp
    src/core/time_series_data.cpp
    src/core/time_series_index.cpp
    src/core/time_series_db.cpp
    src/core/storage_engine.cpp
    src/core/lsm_tree.cpp
    src/core/stream_table.cpp
    src/core/join_result_table.cpp
    src/core/table_manager.cpp
    src/utils/config.cpp
)

# PECJ Integration Mode selection
option(SAGE_TSDB_ENABLE_PECJ "Enable PECJ integration" OFF)
set(PECJ_MODE "INTEGRATED" CACHE STRING "PECJ integration mode: PLUGIN or INTEGRATED")

# Compute library (PECJ Deep Integration)
if(SAGE_TSDB_ENABLE_PECJ AND PECJ_MODE STREQUAL "INTEGRATED")
    message(STATUS "Building PECJ Compute Engine (Deep Integration Mode)")
    add_definitions(-DPECJ_MODE_INTEGRATED)
    
    add_library(sage_tsdb_compute
        src/compute/pecj_compute_engine.cpp
        src/compute/window_scheduler.cpp
    )
    
    target_include_directories(sage_tsdb_compute
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
    )
    
    # Link sage_tsdb_core first
    target_link_libraries(sage_tsdb_compute
        PUBLIC
            sage_tsdb_core
    )
    
    # If PECJ directory is specified, add PECJ support
    if(DEFINED PECJ_DIR)
        get_filename_component(PECJ_DIR_ABS "${PECJ_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(PECJ_INCLUDE_DIR "${PECJ_DIR_ABS}/include")
        set(PECJ_BUILD_DIR "${PECJ_DIR_ABS}/build")
        
        if(EXISTS "${PECJ_INCLUDE_DIR}")
            target_include_directories(sage_tsdb_compute
                PUBLIC
                    ${PECJ_INCLUDE_DIR}
            )
            
            # Find Torch for PECJ support
            if(EXISTS "${PECJ_BUILD_DIR}/CMakeCache.txt")
                file(STRINGS "${PECJ_BUILD_DIR}/CMakeCache.txt" TORCH_DIR_LINE REGEX "^Torch_DIR")
                if(TORCH_DIR_LINE)
                    string(REGEX REPLACE "^Torch_DIR:PATH=" "" TORCH_CMAKE_DIR "${TORCH_DIR_LINE}")
                    set(CMAKE_PREFIX_PATH "${TORCH_CMAKE_DIR};${CMAKE_PREFIX_PATH}")
                endif()
            endif()
            
            find_package(Torch QUIET)
            if(Torch_FOUND)
                message(STATUS "  Torch found for compute engine: ${TORCH_LIBRARIES}")
                target_include_directories(sage_tsdb_compute PUBLIC ${TORCH_INCLUDE_DIRS})
                target_link_libraries(sage_tsdb_compute PUBLIC ${TORCH_LIBRARIES})
                target_compile_definitions(sage_tsdb_compute PUBLIC TORCH_AVAILABLE=1)
            endif()
            
            # Find PECJ library
            find_library(PECJ_LIBRARY
                NAMES IntelliStreamOoOJoin libIntelliStreamOoOJoin
                HINTS ${PECJ_BUILD_DIR} ${PECJ_DIR_ABS}/build
                PATH_SUFFIXES lib lib64 src
            )
            
            if(PECJ_LIBRARY)
                message(STATUS "  PECJ library found for compute engine: ${PECJ_LIBRARY}")
                target_link_libraries(sage_tsdb_compute PUBLIC ${PECJ_LIBRARY})
                target_compile_definitions(sage_tsdb_compute PUBLIC PECJ_LIBRARY_AVAILABLE=1)
                
                # Check if PECJ_FULL_INTEGRATION is requested
                if(PECJ_FULL_INTEGRATION)
                    message(STATUS "  Enabling PECJ_FULL_INTEGRATION for compute engine")
                    target_compile_definitions(sage_tsdb_compute PUBLIC PECJ_FULL_INTEGRATION=1)
                endif()
            endif()
        endif()
    endif()
    
    message(STATUS "Built library: sage_tsdb_compute (with WindowScheduler)")
endif()

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
option(PECJ_FULL_INTEGRATION "Enable full PECJ integration (requires PECJ library)" OFF)

# Set CMAKE_PREFIX_PATH for finding Torch (from PyTorch installation)
if(NOT DEFINED CMAKE_PREFIX_PATH)
    # Try to find torch from Python site-packages
    execute_process(
        COMMAND python3 -c "import torch; print(torch.utils.cmake_prefix_path)"
        OUTPUT_VARIABLE TORCH_CMAKE_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(TORCH_CMAKE_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH ${TORCH_CMAKE_PREFIX})
        message(STATUS "Found PyTorch cmake path: ${TORCH_CMAKE_PREFIX}")
    endif()
endif()

if(BUILD_PLUGINS)
    # Check for PECJ
    if(DEFINED PECJ_DIR)
        # Convert to absolute path if relative
        get_filename_component(PECJ_DIR_ABS "${PECJ_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        set(PECJ_INCLUDE_DIR "${PECJ_DIR_ABS}/include")
        set(PECJ_BUILD_DIR "${PECJ_DIR_ABS}/build")
        set(PECJ_LIB_DIR "${PECJ_BUILD_DIR}/src")
        
        message(STATUS "PECJ directory: ${PECJ_DIR_ABS}")
        message(STATUS "PECJ include directory: ${PECJ_INCLUDE_DIR}")
        message(STATUS "PECJ lib directory: ${PECJ_LIB_DIR}")
        
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
            
            # Check for PECJ library and enable full integration
            if(PECJ_FULL_INTEGRATION)
                # Find Torch first (required by PECJ)
                # Try to find Torch from PECJ's CMakeCache
                if(EXISTS "${PECJ_BUILD_DIR}/CMakeCache.txt")
                    file(STRINGS "${PECJ_BUILD_DIR}/CMakeCache.txt" TORCH_DIR_LINE REGEX "^Torch_DIR")
                    if(TORCH_DIR_LINE)
                        string(REGEX REPLACE "^Torch_DIR:PATH=" "" TORCH_CMAKE_DIR "${TORCH_DIR_LINE}")
                        message(STATUS "Found Torch path from PECJ build: ${TORCH_CMAKE_DIR}")
                        set(CMAKE_PREFIX_PATH "${TORCH_CMAKE_DIR};${CMAKE_PREFIX_PATH}")
                    endif()
                endif()
                
                find_package(Torch QUIET)
                if(Torch_FOUND)
                    message(STATUS "✓ Found Torch: ${TORCH_LIBRARIES}")
                    message(STATUS "  Torch version: ${Torch_VERSION}")
                    message(STATUS "  Torch include dirs: ${TORCH_INCLUDE_DIRS}")
                    message(STATUS "  Torch library dirs: ${TORCH_LIBRARY_DIRS}")
                else()
                    message(WARNING "✗ Torch not found - trying manual path from user Python install")
                    set(TORCH_INSTALL_PREFIX "/home/cdb/.local/lib/python3.10/site-packages/torch")
                    if(EXISTS "${TORCH_INSTALL_PREFIX}/share/cmake/Torch")
                        set(Torch_DIR "${TORCH_INSTALL_PREFIX}/share/cmake/Torch")
                        find_package(Torch QUIET)
                        if(Torch_FOUND)
                            message(STATUS "✓ Found Torch from user install: ${TORCH_LIBRARIES}")
                        endif()
                    endif()
                endif()
                
                if(NOT Torch_FOUND)
                    message(FATAL_ERROR "Torch is required for PECJ_FULL_INTEGRATION but not found. "
                                        "Please install libtorch or disable PECJ_FULL_INTEGRATION")
                endif()
                
                # Find PECJ library (IntelliStreamOoOJoin is the actual library name)
                find_library(PECJ_LIBRARY
                    NAMES IntelliStreamOoOJoin libIntelliStreamOoOJoin OoOJoin libOoOJoin
                    HINTS ${PECJ_BUILD_DIR} ${PECJ_LIB_DIR} ${PECJ_DIR_ABS}/build
                    PATH_SUFFIXES lib lib64 src
                )
                
                if(PECJ_LIBRARY)
                    message(STATUS "Found PECJ library: ${PECJ_LIBRARY}")
                    target_compile_definitions(sage_tsdb_plugins PUBLIC PECJ_FULL_INTEGRATION=1)
                    
                    # Add Torch include directories
                    target_include_directories(sage_tsdb_plugins
                        PUBLIC
                            ${TORCH_INCLUDE_DIRS}
                    )
                    
                    target_link_libraries(sage_tsdb_plugins
                        PUBLIC
                            sage_tsdb_core
                            ${PECJ_LIBRARY}
                            ${TORCH_LIBRARIES}
                    )
                else()
                    message(WARNING "PECJ library not found in ${PECJ_BUILD_DIR}")
                    message(WARNING "Building with stub PECJ support. Run 'make' in PECJ directory first.")
                    target_link_libraries(sage_tsdb_plugins PUBLIC sage_tsdb_core)
                endif()
            else()
                target_link_libraries(sage_tsdb_plugins PUBLIC sage_tsdb_core)
                message(STATUS "Building with stub PECJ support (set -DPECJ_FULL_INTEGRATION=ON for full support)")
            endif()
            
            message(STATUS "Plugin system enabled with PECJ support")
        else()
            message(WARNING "PECJ include directory not found: ${PECJ_INCLUDE_DIR}")
            message(WARNING "Building plugins without PECJ support")
        endif()
    else()
        # Build plugins in Stub mode (without PECJ)
        message(STATUS "PECJ_DIR not set, building plugins in Stub mode")
        message(STATUS "To enable full PECJ support: cmake -DPECJ_DIR=/path/to/PECJ -DPECJ_FULL_INTEGRATION=ON ..")
        
        # Build basic plugin infrastructure without PECJ
        add_subdirectory(src/plugins)
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
        # Editable install: use parent-defined install dir if available, else fallback
        if(DEFINED SAGE_TSDB_INSTALL_DIR)
            set(_lib_install_dest "${SAGE_TSDB_INSTALL_DIR}")
            message(STATUS "sageTSDB: Using parent-defined install dir: ${_lib_install_dest}")
        else()
            set(_lib_install_dest "${CMAKE_CURRENT_SOURCE_DIR}/sage_tsdb")
            message(STATUS "sageTSDB: Using default editable install dir: ${_lib_install_dest}")
        endif()
    else()
        # Wheel build: install to wheel platlib (same directory as Python extension)
        set(_lib_install_dest "${SKBUILD_PLATLIB_DIR}/sage_tsdb")
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
    add_subdirectory(sage_tsdb)
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
if(DEFINED PECJ_DIR AND EXISTS "${PECJ_DIR}/include")
    message(STATUS "  PECJ support: YES (${PECJ_DIR})")
elseif(TARGET sage_tsdb_plugins)
    message(STATUS "  PECJ support: STUB (plugin infrastructure only)")
else()
    message(STATUS "  PECJ support: NO")
endif()
message(STATUS "  OpenMP support: ${ENABLE_OPENMP}")
message(STATUS "")

