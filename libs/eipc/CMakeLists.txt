cmake_minimum_required(VERSION 3.16)

add_library(eipc STATIC
    src/eipc.c
    src/types.c
    src/message.c
    src/frame.c
    src/peer.c
    src/peer_table.c
    src/poller.c
    src/transport_unix.c
)

target_include_directories(eipc PUBLIC
     $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(eipc PRIVATE ${COMMON_COMPILE_OPTIONS})

if(BUILD_EXAMPLES)
    message(STATUS "[EPIC] Building examples...")
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/eipc_brokerd.c")
        add_executable(eipc_brokerd examples/eipc_brokerd.c)
        target_link_libraries(eipc_brokerd PRIVATE eipc)
        target_compile_options(eipc_brokerd PRIVATE ${COMMON_COMPILE_OPTIONS})
    endif()
    
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/eipc_client.c")
        add_executable(eipc_client_app examples/eipc_client.c)
        target_link_libraries(eipc_client_app PRIVATE eipc)
        target_compile_options(eipc_client_app PRIVATE ${COMMON_COMPILE_OPTIONS})
    endif()
endif()
