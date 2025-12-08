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

# Table design demo
add_executable(table_design_demo
    table_design_demo.cpp
)

target_link_libraries(table_design_demo
    PRIVATE
        sage_tsdb_core
)

# WindowScheduler demo (only if compute library is built)
if(TARGET sage_tsdb_compute)
    add_executable(window_scheduler_demo
        window_scheduler_demo.cpp
    )
    
    target_link_libraries(window_scheduler_demo
        PRIVATE
            sage_tsdb_core
            sage_tsdb_compute
            sage_tsdb_plugins
    )
    
    message(STATUS "Building window_scheduler_demo")
endif()

# Deep Integration Demo (only if compute library is built in INTEGRATED mode)
if(TARGET sage_tsdb_compute AND PECJ_MODE STREQUAL "INTEGRATED")
    add_executable(deep_integration_demo
        deep_integration_demo.cpp
    )
    
    target_link_libraries(deep_integration_demo
        PRIVATE
            sage_tsdb_core
            sage_tsdb_compute
    )
    
    target_compile_definitions(deep_integration_demo
        PRIVATE
            PECJ_MODE_INTEGRATED
    )
    
    message(STATUS "Building deep_integration_demo (PECJ Integrated Mode)")
endif()

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
    
    # Add PECJ demos to install targets
    list(APPEND EXAMPLE_TARGETS 
        pecj_replay_demo 
        integrated_demo 
        performance_benchmark
    )
    
    message(STATUS "  - pecj_replay_demo")
    message(STATUS "  - integrated_demo")
    message(STATUS "  - performance_benchmark")
    
    # Copy demo configuration files
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/demo_configs.json
        ${CMAKE_CURRENT_BINARY_DIR}/demo_configs.json
        COPYONLY
    )
    
endif()

# Install examples (optional)
set(EXAMPLE_TARGETS persistence_example table_design_demo)
if(TARGET plugin_usage_example)
    list(APPEND EXAMPLE_TARGETS plugin_usage_example)
endif()
if(TARGET window_scheduler_demo)
    list(APPEND EXAMPLE_TARGETS window_scheduler_demo)
endif()

install(TARGETS ${EXAMPLE_TARGETS}
    RUNTIME DESTINATION bin/examples
)

# Install demo documentation and configs
install(FILES 
    README.md
    demo_configs.json
    DESTINATION share/doc/sageTSDB/examples
)
