cmake_minimum_required(VERSION 3.15)

# Python bindings are optional and require pybind11
# This is a placeholder for future pybind11 integration

# For now, just create a simple __init__.py
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/__init__.py "")

# Installation of Python modules
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages/sage/middleware/components/sage_tsdb/python
    FILES_MATCHING PATTERN "*.py"
)
