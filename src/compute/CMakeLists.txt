# CMakeLists.txt for PECJ Compute Engine (Deep Integration Mode)

cmake_minimum_required(VERSION 3.14)

# ========== PECJ Integration Configuration ==========

# Option to enable PECJ integration
option(SAGE_TSDB_ENABLE_PECJ "Enable PECJ integration" OFF)

# PECJ mode selection: PLUGIN or INTEGRATED
set(PECJ_MODE "INTEGRATED" CACHE STRING "PECJ integration mode: PLUGIN or INTEGRATED")
set_property(CACHE PECJ_MODE PROPERTY STRINGS "PLUGIN" "INTEGRATED")

# PECJ source/build directory
set(PECJ_DIR "" CACHE PATH "Path to PECJ source or build directory")

# ========== Conditional Compilation ==========

if(SAGE_TSDB_ENABLE_PECJ)
    message(STATUS "==========================================")
    message(STATUS "PECJ Integration: ENABLED")
    message(STATUS "PECJ Mode: ${PECJ_MODE}")
    message(STATUS "PECJ Directory: ${PECJ_DIR}")
    message(STATUS "==========================================")
    
    # Find PECJ library
    if(PECJ_DIR)
        find_package(PECJ QUIET PATHS ${PECJ_DIR})
        if(NOT PECJ_FOUND)
            message(WARNING "PECJ not found, using stub mode")
            add_definitions(-DPECJ_STUB_MODE)
        else()
            message(STATUS "PECJ Found: ${PECJ_LIBRARIES}")
            add_definitions(-DPECJ_FULL_INTEGRATION)
        endif()
    else()
        message(WARNING "PECJ_DIR not specified, using stub mode")
        add_definitions(-DPECJ_STUB_MODE)
    endif()
    
    # ========== Mode-Specific Configuration ==========
    
    if(PECJ_MODE STREQUAL "PLUGIN")
        message(STATUS "Building in PLUGIN mode (traditional adapter)")
        add_definitions(-DPECJ_MODE_PLUGIN)
        
        # Build traditional plugin mode
        set(PECJ_PLUGIN_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/adapters/pecj_adapter.cpp
        )
        
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/adapters/pecj_adapter.cpp")
            add_library(sage_tsdb_pecj_plugin STATIC
                ${PECJ_PLUGIN_SOURCES}
            )
            
            target_include_directories(sage_tsdb_pecj_plugin
                PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
            )
            
            if(PECJ_FOUND)
                target_link_libraries(sage_tsdb_pecj_plugin
                    PRIVATE ${PECJ_LIBRARIES}
                )
            endif()
            
            message(STATUS "Built library: sage_tsdb_pecj_plugin")
        else()
            message(WARNING "Plugin sources not found, skipping plugin build")
        endif()
        
    elseif(PECJ_MODE STREQUAL "INTEGRATED")
        message(STATUS "Building in INTEGRATED mode (deep fusion)")
        add_definitions(-DPECJ_MODE_INTEGRATED)
        
        # Build deep integration mode
        set(PECJ_ENGINE_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/src/compute/pecj_compute_engine.cpp
        )
        
        set(PECJ_ENGINE_HEADERS
            ${CMAKE_CURRENT_SOURCE_DIR}/include/sage_tsdb/compute/pecj_compute_engine.h
        )
        
        add_library(sage_tsdb_pecj_engine STATIC
            ${PECJ_ENGINE_SOURCES}
            ${PECJ_ENGINE_HEADERS}
        )
        
        target_include_directories(sage_tsdb_pecj_engine
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        
        if(PECJ_FOUND)
            target_link_libraries(sage_tsdb_pecj_engine
                PRIVATE ${PECJ_LIBRARIES}
            )
            target_include_directories(sage_tsdb_pecj_engine
                PRIVATE ${PECJ_INCLUDE_DIRS}
            )
        endif()
        
        # Link with sageTSDB core (when available)
        # target_link_libraries(sage_tsdb_pecj_engine
        #     PRIVATE sage_tsdb_core
        # )
        
        message(STATUS "Built library: sage_tsdb_pecj_engine")
        
        # ========== Examples ==========
        
        option(BUILD_PECJ_EXAMPLES "Build PECJ examples" ON)
        
        if(BUILD_PECJ_EXAMPLES)
            add_executable(pecj_compute_example
                ${CMAKE_CURRENT_SOURCE_DIR}/examples/pecj_compute_example.cpp
            )
            
            target_link_libraries(pecj_compute_example
                PRIVATE sage_tsdb_pecj_engine
            )
            
            message(STATUS "Built executable: pecj_compute_example")
        endif()
        
        # ========== Tests ==========
        
        option(BUILD_PECJ_TESTS "Build PECJ tests" ON)
        
        if(BUILD_PECJ_TESTS)
            find_package(GTest QUIET)
            
            if(GTest_FOUND)
                enable_testing()
                
                add_executable(pecj_compute_engine_test
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/compute/pecj_compute_engine_test.cpp
                )
                
                target_link_libraries(pecj_compute_engine_test
                    PRIVATE sage_tsdb_pecj_engine
                    PRIVATE GTest::gtest
                    PRIVATE GTest::gmock
                    PRIVATE GTest::gtest_main
                )
                
                add_test(NAME PECJComputeEngineTest 
                         COMMAND pecj_compute_engine_test)
                
                message(STATUS "Built test: pecj_compute_engine_test")
            else()
                message(WARNING "GTest not found, skipping tests")
            endif()
        endif()
        
    else()
        message(FATAL_ERROR "Invalid PECJ_MODE: ${PECJ_MODE}. Must be PLUGIN or INTEGRATED")
    endif()
    
else()
    message(STATUS "==========================================")
    message(STATUS "PECJ Integration: DISABLED")
    message(STATUS "Using stub implementation")
    message(STATUS "==========================================")
    
    add_definitions(-DPECJ_STUB_MODE)
    
    # Build stub implementation
    # add_library(sage_tsdb_pecj_stub INTERFACE)
    # target_include_directories(sage_tsdb_pecj_stub INTERFACE
    #     ${CMAKE_CURRENT_SOURCE_DIR}/include
    # )
endif()

# ========== Installation ==========

if(SAGE_TSDB_ENABLE_PECJ AND PECJ_MODE STREQUAL "INTEGRATED")
    install(TARGETS sage_tsdb_pecj_engine
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/sage_tsdb/compute
        DESTINATION include/sage_tsdb
        FILES_MATCHING PATTERN "*.h"
    )
endif()

# ========== Build Summary ==========

message(STATUS "==========================================")
message(STATUS "PECJ Build Configuration Summary:")
message(STATUS "  Enabled: ${SAGE_TSDB_ENABLE_PECJ}")
if(SAGE_TSDB_ENABLE_PECJ)
    message(STATUS "  Mode: ${PECJ_MODE}")
    message(STATUS "  PECJ Found: ${PECJ_FOUND}")
    if(PECJ_MODE STREQUAL "INTEGRATED")
        message(STATUS "  Examples: ${BUILD_PECJ_EXAMPLES}")
        message(STATUS "  Tests: ${BUILD_PECJ_TESTS}")
    endif()
endif()
message(STATUS "==========================================")
