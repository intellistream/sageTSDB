cmake_minimum_required(VERSION 3.15)

# Find Python
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

# Find or fetch pybind11 (check if parent project already provided it)
if(NOT TARGET pybind11::module)
    find_package(pybind11 CONFIG QUIET)
    
    if(NOT pybind11_FOUND)
        message(STATUS "pybind11 not found, fetching from GitHub...")
        include(FetchContent)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.13.0
        )
        FetchContent_MakeAvailable(pybind11)
    endif()
else()
    message(STATUS "Using pybind11 provided by parent project")
endif()

# Create Python bindings module
pybind11_add_module(_sage_tsdb bindings.cpp)

# Link against SAGE TSDB libraries
target_link_libraries(_sage_tsdb PRIVATE
    sage_tsdb_core
    sage_tsdb_algorithms
)

# Include directories
target_include_directories(_sage_tsdb PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Set module properties
set_target_properties(_sage_tsdb PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    PREFIX "${PYTHON_MODULE_PREFIX}"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
    # SOTA: Use RPATH $ORIGIN to find libraries in same directory
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH "$ORIGIN"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

# Install the module
# SOTA: Conditional install based on build context
if(DEFINED SKBUILD)
    if(SKBUILD_STATE STREQUAL "editable")
        # Editable install: install to source tree directly
        set(_install_dest "${CMAKE_CURRENT_SOURCE_DIR}/../../python")
        # Also install C++ libraries to same location
        install(TARGETS sage_tsdb_core sage_tsdb_algorithms
            LIBRARY DESTINATION ${_install_dest}
            ARCHIVE DESTINATION ${_install_dest}
            RUNTIME DESTINATION ${_install_dest}
            COMPONENT python
        )
    else()
        # Wheel build: install to wheel platlib
        set(_install_dest "${SKBUILD_PLATLIB_DIR}/sage/middleware/components/sage_tsdb/python")
    endif()
    
    install(TARGETS _sage_tsdb
        LIBRARY DESTINATION ${_install_dest}
        COMPONENT python
    )
else()
    # Standalone C++ project build
    install(TARGETS _sage_tsdb
        LIBRARY DESTINATION python
        COMPONENT python
    )
endif()
