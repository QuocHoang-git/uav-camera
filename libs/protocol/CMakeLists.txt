cmake_minimum_required(VERSION 3.16)

add_library(protocol STATIC
    src/proto_types.c
    src/proto_bridge.c
)

target_include_directories(protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(protocol PUBLIC eipc)

target_compile_options(protocol PRIVATE ${COMMON_COMPILE_OPTIONS})

if(BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/test_protocol.c")
        add_executable(test_protocol examples/test_protocol.c)
        target_link_libraries(test_protocol PRIVATE protocol)
        target_compile_options(test_protocol PRIVATE ${COMMON_COMPILE_OPTIONS})
    endif()
endif()
