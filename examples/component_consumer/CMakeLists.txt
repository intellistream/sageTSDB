cmake_minimum_required(VERSION 3.15)
project(sage_tsdb_pecj_consumer LANGUAGES CXX)

find_package(sageTSDB CONFIG REQUIRED COMPONENTS compute)

add_executable(pecj_consumer main.cpp)
target_link_libraries(pecj_consumer PRIVATE sageTSDB::compute)
target_compile_features(pecj_consumer PRIVATE cxx_std_20)
