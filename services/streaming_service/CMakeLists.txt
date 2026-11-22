cmake_minimum_required(VERSION 3.16)

add_executable(streaming_service
    src/main.cpp
    src/streaming_app.cpp
    src/streaming_ipc_client.cpp
    src/rtsp_server.cpp
)

target_include_directories(streaming_service PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(streaming_service PRIVATE
    protocol
    eipc
    pthread
)

target_compile_features(streaming_service PRIVATE cxx_std_17)
target_compile_options(streaming_service PRIVATE ${COMMON_COMPILE_OPTIONS})
