# This file is injected only into the out-of-tree PECJ delivery build.
# Keep compiler-recorded source locations stable and free of build-host paths.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:CXX>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=PECJ>"
        "$<$<COMPILE_LANGUAGE:CXX>:-ffile-prefix-map=${CMAKE_BINARY_DIR}=PECJ-build>"
    )

    if(PECJ_DELIVERY_TORCH_ROOT)
        add_compile_options(
            "$<$<COMPILE_LANGUAGE:CXX>:-ffile-prefix-map=${PECJ_DELIVERY_TORCH_ROOT}=torch>"
        )
    endif()
endif()
