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

# ============================================================================
# PECJ Integration Demos (only if PECJ plugin is available)
# ============================================================================
if(TARGET sage_tsdb_plugins)
    message(STATUS "Building PECJ integration demos")
    
    # 1. PECJ Replay Demo - Basic stream join demonstration
    add_executable(pecj_replay_demo
        pecj_replay_demo.cpp
    )
    
    target_link_libraries(pecj_replay_demo
        PRIVATE
            sage_tsdb_core
            sage_tsdb_plugins
    )
    
    target_include_directories(pecj_replay_demo
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
    
    # 2. Integrated Demo - PECJ + Fault Detection
    add_executable(integrated_demo
        integrated_demo.cpp
    )
    
    target_link_libraries(integrated_demo
        PRIVATE
            sage_tsdb_core
            sage_tsdb_plugins
    )
    
    target_include_directories(integrated_demo
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
    
    # 3. Performance Benchmark - Comprehensive performance testing
    add_executable(performance_benchmark
        performance_benchmark.cpp
    )
    
    target_link_libraries(performance_benchmark
        PRIVATE
            sage_tsdb_core
            sage_tsdb_plugins
    )
    
    target_include_directories(performance_benchmark
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
    
    # 4. Integrated Mode Test - ResourceManager verification
    add_executable(test_integrated_mode
        test_integrated_mode.cpp
    )
    
    target_link_libraries(test_integrated_mode
        PRIVATE
            sage_tsdb_core
            sage_tsdb_plugins
    )
    
    target_include_directories(test_integrated_mode
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
    )
    
    # Add PECJ demos to install targets
    list(APPEND EXAMPLE_TARGETS 
        pecj_replay_demo 
        integrated_demo 
        performance_benchmark
        test_integrated_mode
    )
    
    message(STATUS "  - pecj_replay_demo")
    message(STATUS "  - integrated_demo")
    message(STATUS "  - performance_benchmark")
    message(STATUS "  - test_integrated_mode")
    
    # Copy demo configuration files
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_configs.json
        ${CMAKE_CURRENT_BINARY_DIR}/demo_configs.json
        COPYONLY
    )
    
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/README_PECJ_DEMO.md
        ${CMAKE_CURRENT_BINARY_DIR}/README_PECJ_DEMO.md
        COPYONLY
    )
    
endif()

# Install examples (optional)
set(EXAMPLE_TARGETS persistence_example)
if(TARGET plugin_usage_example)
    list(APPEND EXAMPLE_TARGETS plugin_usage_example)
endif()

install(TARGETS ${EXAMPLE_TARGETS}
    RUNTIME DESTINATION bin/examples
)

# Install demo documentation and configs
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/README_PECJ_DEMO.md)
    install(FILES 
        README_PECJ_DEMO.md
        demo_configs.json
        DESTINATION share/doc/sageTSDB/examples
    )
endif()
