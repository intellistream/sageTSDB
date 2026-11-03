cmake_minimum_required(VERSION 3.15)
project(sageTSDB VERSION 0.2.0 LANGUAGES CXX)

include(GNUInstallDirs)

# Set C++ standard (C++20 required for PECJ integration)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force old ABI for compatibility with PyTorch/PECJ (they use old ABI)
add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

# Options
option(BUILD_TESTS "Build tests" ON)
# Auto-enable Python bindings for wheel builds
if(DEFINED SKBUILD)
    set(BUILD_PYTHON_BINDINGS ON CACHE BOOL "Build Python bindings" FORCE)
else()
    option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
endif()
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_OPENMP "Enable OpenMP support" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(SAGE_TSDB_BUILD_COMPUTE_CLI "Build the PECJ compute component CLI" ON)
option(SAGE_TSDB_BUILD_PECJ_C_API "Build the stable PECJ C ABI" ON)

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
    src/core/storage_backend.cpp
    src/core/backends/memory_backend.cpp
    src/utils/config.cpp
)

# PECJ Integration Mode selection
option(SAGE_TSDB_ENABLE_PECJ "Enable PECJ integration" OFF)
option(SAGE_TSDB_ENABLE_SRTFD "Enable SRTFD stateless compute engine" ON)
option(SAGE_TSDB_ENABLE_SRTFD_TORCH "Enable trained SRTFD TorchScript inference" OFF)
option(PECJ_FULL_INTEGRATION "Enable full PECJ integration (requires PECJ library)" OFF)
set(PECJ_MODE "INTEGRATED" CACHE STRING "PECJ integration mode: PLUGIN or INTEGRATED")
set(PECJ_DIR "" CACHE PATH "Path to the PECJ source tree")
set(PECJ_INCLUDE_DIR "" CACHE PATH "Path to the PECJ public headers")
set(PECJ_LIBRARY "" CACHE FILEPATH "Path to the PECJ library")

# Compute library (stateless compute engines)
set(SAGE_TSDB_COMPUTE_SOURCES)

if(SAGE_TSDB_ENABLE_SRTFD)
    list(APPEND SAGE_TSDB_COMPUTE_SOURCES
        src/compute/srtfd_compute_engine.cpp
    )
endif()

if(SAGE_TSDB_ENABLE_PECJ AND PECJ_MODE STREQUAL "INTEGRATED")
    list(APPEND SAGE_TSDB_COMPUTE_SOURCES
        src/compute/pecj_compute_engine.cpp
        src/compute/compute_state_manager.cpp
        src/compute/window_scheduler.cpp
    )
endif()

if(SAGE_TSDB_COMPUTE_SOURCES)
    add_library(sage_tsdb_compute
        ${SAGE_TSDB_COMPUTE_SOURCES}
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

    set_target_properties(sage_tsdb_compute PROPERTIES
        EXPORT_NAME compute
        INSTALL_RPATH "$ORIGIN"
    )
    add_library(sageTSDB::compute ALIAS sage_tsdb_compute)

    if(SAGE_TSDB_ENABLE_SRTFD)
        target_compile_definitions(sage_tsdb_compute PUBLIC SAGE_TSDB_ENABLE_SRTFD=1)
        message(STATUS "Building SRTFD Compute Engine (Stateless Mode)")
    endif()
endif()

# Optional neural diagnosis works independently of PECJ.
if(SAGE_TSDB_ENABLE_SRTFD_TORCH)
    if(NOT SAGE_TSDB_ENABLE_SRTFD)
        message(FATAL_ERROR "SRTFD_TORCH requires SAGE_TSDB_ENABLE_SRTFD=ON")
    endif()
    find_package(Torch REQUIRED)
    target_include_directories(sage_tsdb_compute PRIVATE ${TORCH_INCLUDE_DIRS})
    target_link_libraries(sage_tsdb_compute PRIVATE ${TORCH_LIBRARIES})
    target_compile_definitions(sage_tsdb_compute PUBLIC SAGE_TSDB_SRTFD_TORCH=1)
    add_executable(sage_tsdb_srtfd tools/srtfd_compute_cli.cpp)
    target_link_libraries(sage_tsdb_srtfd PRIVATE sage_tsdb_compute)
    set_target_properties(sage_tsdb_srtfd PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

# PECJ Compute Engine (Deep Integration)
if(SAGE_TSDB_ENABLE_PECJ AND PECJ_MODE STREQUAL "INTEGRATED" AND TARGET sage_tsdb_compute)
    message(STATUS "Building PECJ Compute Engine (Deep Integration Mode)")
    target_compile_definitions(sage_tsdb_compute PUBLIC PECJ_MODE_INTEGRATED=1)
    
    # PECJ_LIBRARY may point to a clean out-of-tree build instead of
    # PECJ_DIR/build, which is useful when producing a delivery package.
    if(PECJ_DIR OR PECJ_INCLUDE_DIR)
        if(PECJ_DIR)
            get_filename_component(PECJ_DIR_ABS "${PECJ_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        else()
            get_filename_component(PECJ_DIR_ABS "${PECJ_INCLUDE_DIR}/.." ABSOLUTE)
        endif()
        if(NOT PECJ_INCLUDE_DIR)
            set(PECJ_INCLUDE_DIR "${PECJ_DIR_ABS}/include")
        endif()
        set(PECJ_BUILD_DIR "${PECJ_DIR_ABS}/build")
        
        if(EXISTS "${PECJ_INCLUDE_DIR}")
            target_include_directories(sage_tsdb_compute
                PRIVATE
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
                target_include_directories(sage_tsdb_compute PRIVATE ${TORCH_INCLUDE_DIRS})
                target_link_libraries(sage_tsdb_compute PRIVATE ${TORCH_LIBRARIES})
                target_compile_definitions(sage_tsdb_compute PRIVATE TORCH_AVAILABLE=1)
            elseif(PECJ_FULL_INTEGRATION)
                message(FATAL_ERROR "PECJ_FULL_INTEGRATION requires Torch, but Torch was not found")
            endif()
            
            # Find PECJ library
            if(NOT PECJ_LIBRARY)
                find_library(PECJ_LIBRARY
                    NAMES IntelliStreamOoOJoin libIntelliStreamOoOJoin
                    HINTS ${PECJ_BUILD_DIR} ${PECJ_DIR_ABS}/build
                    PATH_SUFFIXES lib lib64 src
                )
            endif()
            
            if(PECJ_LIBRARY)
                message(STATUS "  PECJ library found for compute engine: ${PECJ_LIBRARY}")
                target_link_libraries(sage_tsdb_compute PRIVATE ${PECJ_LIBRARY})
                target_compile_definitions(sage_tsdb_compute PRIVATE PECJ_LIBRARY_AVAILABLE=1)
                
                # Check if PECJ_FULL_INTEGRATION is requested
                if(PECJ_FULL_INTEGRATION)
                    message(STATUS "  Enabling PECJ_FULL_INTEGRATION for compute engine")
                    target_compile_definitions(sage_tsdb_compute PUBLIC PECJ_FULL_INTEGRATION=1)
                endif()
            elseif(PECJ_FULL_INTEGRATION)
                message(FATAL_ERROR "PECJ_FULL_INTEGRATION requires PECJ_LIBRARY")
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

target_compile_definitions(sage_tsdb_core
    PUBLIC _GLIBCXX_USE_CXX11_ABI=0
)

set_target_properties(sage_tsdb_core PROPERTIES
    EXPORT_NAME core
    INSTALL_RPATH "$ORIGIN"
)
add_library(sageTSDB::core ALIAS sage_tsdb_core)

# ---------------------------------------------------------------------------
# Dameng (达梦) storage backend adapter (skeleton; deliverable D3)
#
# OFF by default: the skeleton compiles with NO Dameng driver dependency, and
# when disabled the "dameng" backend is simply not registered (selecting it via
# StorageBackendConfig then throws "unknown backend" -- no silent fallback).
#
# When the DM-owning side wires the real driver, enable this option and fill in
# the include/link block below (keeping ABI _GLIBCXX_USE_CXX11_ABI=0).
# ---------------------------------------------------------------------------
option(SAGE_TSDB_ENABLE_DM "Enable Dameng (达梦) storage backend adapter" OFF)
if(SAGE_TSDB_ENABLE_DM)
    target_sources(sage_tsdb_core PRIVATE src/core/backends/dameng_backend.cpp)
    target_compile_definitions(sage_tsdb_core PUBLIC SAGE_TSDB_ENABLE_DM=1)
    message(STATUS "Dameng storage backend: ENABLED (skeleton; driver TODO)")

    # TODO(DM): locate and link the real Dameng driver here. Example outline:
    #   find_path(DM_INCLUDE_DIR NAMES DPI.h HINTS $ENV{DM_HOME}/include)
    #   find_library(DM_LIBRARY NAMES dmdpi dodbc HINTS $ENV{DM_HOME}/bin $ENV{DM_HOME}/lib)
    #   if(DM_INCLUDE_DIR AND DM_LIBRARY)
    #     target_include_directories(sage_tsdb_core PRIVATE ${DM_INCLUDE_DIR})
    #     target_link_libraries(sage_tsdb_core PUBLIC ${DM_LIBRARY})
    #     target_compile_definitions(sage_tsdb_core PUBLIC SAGE_TSDB_DM_DRIVER_AVAILABLE=1)
    #   else()
    #     message(FATAL_ERROR "SAGE_TSDB_ENABLE_DM=ON but Dameng driver not found")
    #   endif()
else()
    message(STATUS "Dameng storage backend: disabled (default)")
endif()

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

set_target_properties(sage_tsdb_algorithms PROPERTIES
    EXPORT_NAME algorithms
    INSTALL_RPATH "$ORIGIN"
)
add_library(sageTSDB::algorithms ALIAS sage_tsdb_algorithms)

# Plugins library (optional, if PECJ is available)
option(BUILD_PLUGINS "Build plugin system" ON)

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
    if(PECJ_DIR)
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
set_target_properties(sage_tsdb PROPERTIES EXPORT_NAME sage_tsdb)
add_library(sageTSDB::sage_tsdb ALIAS sage_tsdb)

# Standalone executable shipped with the PECJ compute component. It provides a
# deterministic in-memory self-test and can execute a window join over CSV data.
if(SAGE_TSDB_BUILD_COMPUTE_CLI
   AND TARGET sage_tsdb_compute
   AND SAGE_TSDB_ENABLE_PECJ
   AND PECJ_MODE STREQUAL "INTEGRATED")
    add_executable(sage_tsdb_pecj tools/pecj_compute_cli.cpp)
    target_link_libraries(sage_tsdb_pecj PRIVATE sage_tsdb_compute)
    target_compile_definitions(sage_tsdb_pecj PRIVATE
        SAGE_TSDB_COMPONENT_VERSION="${PROJECT_VERSION}"
    )
    set_target_properties(sage_tsdb_pecj PROPERTIES
        INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"
    )
endif()

# Stable C ABI for consumers that must not depend on the libstdc++ ABI. The
# implementation remains C++, while the public surface contains only C types.
if(SAGE_TSDB_BUILD_PECJ_C_API
   AND TARGET sage_tsdb_compute
   AND SAGE_TSDB_ENABLE_PECJ
   AND PECJ_MODE STREQUAL "INTEGRATED")
    add_library(sage_tsdb_pecj_c SHARED src/c_api/pecj_c_api.cpp)
    target_include_directories(sage_tsdb_pecj_c
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(sage_tsdb_pecj_c PRIVATE sage_tsdb_compute sage_tsdb_core)
    target_compile_definitions(sage_tsdb_pecj_c
        PRIVATE
            SAGE_TSDB_PECJ_C_BUILDING=1
            SAGE_TSDB_COMPONENT_VERSION="${PROJECT_VERSION}"
    )
    set_target_properties(sage_tsdb_pecj_c PROPERTIES
        EXPORT_NAME pecj_c
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        VERSION 1.0.0
        SOVERSION 1
        INSTALL_RPATH "$ORIGIN"
    )
    if(UNIX AND NOT APPLE)
        target_link_options(sage_tsdb_pecj_c PRIVATE
            "LINKER:--version-script=${PROJECT_SOURCE_DIR}/cmake/pecj_c_api.map"
        )
    endif()
    add_library(sageTSDB::pecj_c ALIAS sage_tsdb_pecj_c)
endif()

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
    set(SAGE_TSDB_INSTALL_TARGETS
        sage_tsdb
        sage_tsdb_core
        sage_tsdb_algorithms
    )
    if(TARGET sage_tsdb_compute)
        list(APPEND SAGE_TSDB_INSTALL_TARGETS sage_tsdb_compute)
    endif()
    if(TARGET sage_tsdb_pecj_c)
        list(APPEND SAGE_TSDB_INSTALL_TARGETS sage_tsdb_pecj_c)
    endif()

    install(TARGETS ${SAGE_TSDB_INSTALL_TARGETS}
        EXPORT sageTSDBTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    if(TARGET sage_tsdb_pecj)
        install(TARGETS sage_tsdb_pecj
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()
    if(TARGET sage_tsdb_srtfd)
        install(TARGETS sage_tsdb_srtfd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(PROGRAMS scripts/demo/sage_tsdb_research DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(DIRECTORY sage_tsdb/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/sageTSDB/python/sage_tsdb
            FILES_MATCHING PATTERN "*.py" PATTERN "__pycache__" EXCLUDE)
        install(FILES docs/compute/RESEARCH_ENGINE.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/compute)
        install(FILES docs/licenses/SRTFD-LICENSE
            DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses)
        # Package the exact upstream source and saved networks, not copied layer definitions.
        install(FILES ${PROJECT_SOURCE_DIR}/../SRTFD/models/network.py
            DESTINATION ${CMAKE_INSTALL_DATADIR}/upstream/SRTFD/models)
        install(DIRECTORY ${PROJECT_SOURCE_DIR}/../PECJ/benchmark/torchscripts/linearVAE/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/upstream/PECJ/torchscripts/linearVAE
            FILES_MATCHING PATTERN "*.pt" PATTERN "*.py"
            PATTERN "__pycache__" EXCLUDE)
    endif()

    # Export targets for standalone build
    install(EXPORT sageTSDBTargets
        FILE sageTSDBTargets.cmake
        NAMESPACE sageTSDB::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sageTSDB
    )

    # Install headers
    install(DIRECTORY include/sage_tsdb
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )

    # Create config file
    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/sageTSDBConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    set(SAGE_TSDB_CONFIG_NEEDS_OPENMP ${OpenMP_CXX_FOUND})
    configure_package_config_file(cmake/sageTSDBConfig.cmake.in
        "${PROJECT_BINARY_DIR}/sageTSDBConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sageTSDB
    )

    install(FILES
        "${PROJECT_BINARY_DIR}/sageTSDBConfig.cmake"
        "${PROJECT_BINARY_DIR}/sageTSDBConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sageTSDB
    )

    if(TARGET sage_tsdb_compute)
        install(FILES
            docs/compute/PECJ_COMPUTE_LAYER_INTERFACE.md
            docs/compute/PECJ_COMPUTE_COMPONENT_DELIVERY.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/compute
        )
        install(DIRECTORY examples/component_consumer/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/sageTSDB/component_consumer
        )
    endif()
    if(TARGET sage_tsdb_pecj_c)
        install(FILES docs/compute/PECJ_C_API.md
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/compute
        )
        install(DIRECTORY examples/c_component_consumer/
            DESTINATION ${CMAKE_INSTALL_DATADIR}/sageTSDB/c_component_consumer
        )
    endif()
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
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Print configuration
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build Python bindings: ${BUILD_PYTHON_BINDINGS}")
message(STATUS "  Build shared libs: ${BUILD_SHARED_LIBS}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build plugins: ${BUILD_PLUGINS}")
message(STATUS "  SRTFD compute support: ${SAGE_TSDB_ENABLE_SRTFD}")
if(PECJ_DIR AND EXISTS "${PECJ_DIR}/include")
    message(STATUS "  PECJ support: YES (${PECJ_DIR})")
elseif(TARGET sage_tsdb_plugins)
    message(STATUS "  PECJ support: STUB (plugin infrastructure only)")
else()
    message(STATUS "  PECJ support: NO")
endif()
message(STATUS "  OpenMP support: ${ENABLE_OPENMP}")
message(STATUS "")
