cmake_minimum_required(VERSION 3.15)
project(sage_tsdb_pecj_c_consumer LANGUAGES C)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS pecj_c)

add_executable(pecj_c_consumer main.c)
set_target_properties(pecj_c_consumer PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
target_link_libraries(pecj_c_consumer PRIVATE sageTSDB::pecj_c)
