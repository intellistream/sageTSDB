cmake_minimum_required(VERSION 3.15)

# Examples - optional demonstrations of sageTSDB usage

# Persistence example
add_executable(persistence_example
    persistence_example.cpp
)

target_link_libraries(persistence_example
    PRIVATE
        sage_tsdb_core
)

# Plugin usage example (only if plugins are built)
if(TARGET sage_tsdb_plugins)
    add_executable(plugin_usage_example
        plugin_usage_example.cpp
    )
    
    target_link_libraries(plugin_usage_example
        PRIVATE
            sage_tsdb_core
            sage_tsdb_plugins
    )
    
    message(STATUS "Building plugin_usage_example")
endif()

# Install examples (optional)
set(EXAMPLE_TARGETS persistence_example)
if(TARGET plugin_usage_example)
    list(APPEND EXAMPLE_TARGETS plugin_usage_example)
endif()

install(TARGETS ${EXAMPLE_TARGETS}
    RUNTIME DESTINATION bin/examples
)
