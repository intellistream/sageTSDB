cmake_minimum_required(VERSION 3.15)

# Find Python and pybind11
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG QUIET)

if(NOT pybind11_FOUND)
    message(STATUS "pybind11 not found, fetching from GitHub...")
    include(FetchContent)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.11.1
    )
    FetchContent_MakeAvailable(pybind11)
endif()

# Create Python bindings module
pybind11_add_module(_sage_tsdb bindings.cpp)

# Link against SAGE TSDB libraries
target_link_libraries(_sage_tsdb PRIVATE
    sage_tsdb_core
    sage_tsdb_algorithms
)

# Set module properties
set_target_properties(_sage_tsdb PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    PREFIX "${PYTHON_MODULE_PREFIX}"
    SUFFIX "${PYTHON_MODULE_EXTENSION}"
)

# Install the module to the correct Python package location
install(TARGETS _sage_tsdb
    LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/../python
    COMPONENT python
)

# Also create __init__.py for the package
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
"\"\"\"
SAGE TSDB Python Bindings
High-performance time series database for streaming data
\"\"\"

from ._sage_tsdb import *

__all__ = ['TimeSeriesData', 'TimeSeriesDB', 'TimeSeriesIndex', 'TimeRange', 'QueryConfig']
")

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/__init__.py
    DESTINATION ${CMAKE_INSTALL_PREFIX}/../python
    COMPONENT python
)

