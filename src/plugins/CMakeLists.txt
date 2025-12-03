# Plugins CMakeLists.txt
# Build system for sageTSDB plugin architecture

# Plugin Manager
add_library(sage_tsdb_plugins
    plugin_manager.cpp
    resource_manager.cpp
    adapters/pecj_adapter.cpp
    adapters/fault_detection_adapter.cpp
)

target_include_directories(sage_tsdb_plugins PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Add PECJ include directory only if available
if(DEFINED PECJ_DIR AND EXISTS "${PECJ_DIR}/include")
    target_include_directories(sage_tsdb_plugins PUBLIC
        ${PECJ_DIR}/include
    )
endif()

target_link_libraries(sage_tsdb_plugins PUBLIC
    sage_tsdb_core
    ${CMAKE_THREAD_LIBS_INIT}
)

# Link to PECJ library if available
if(EXISTS "${CMAKE_SOURCE_DIR}/../PECJ/build")
    target_link_directories(sage_tsdb_plugins PUBLIC
        ${CMAKE_SOURCE_DIR}/../PECJ/build/src
    )
    # Note: Adjust library name based on PECJ's actual output
    # target_link_libraries(sage_tsdb_plugins PUBLIC pecj_lib)
endif()

# Install targets
install(TARGETS sage_tsdb_plugins
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/sage_tsdb/plugins
    DESTINATION include/sage_tsdb
    FILES_MATCHING PATTERN "*.h"
)
