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

# Install examples (optional)
install(TARGETS persistence_example
    RUNTIME DESTINATION bin/examples
)
