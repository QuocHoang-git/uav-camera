cmake_minimum_required(VERSION 3.16)

add_executable(encoder_service
    src/main.cpp
    src/encoder_app.cpp
    src/encoder_device.cpp
    src/encoder_ipc_client.cpp
)

target_include_directories(encoder_service PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(encoder_service PRIVATE
    protocol
    hal
    eipc
    pthread
)

target_compile_features(encoder_service PRIVATE cxx_std_17)
target_compile_options(encoder_service PRIVATE ${COMMON_COMPILE_OPTIONS})
