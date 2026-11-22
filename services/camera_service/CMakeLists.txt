cmake_minimum_required(VERSION 3.16)

add_executable(camera_service
    src/main.cpp
    src/camera_app.cpp
    src/camera_device.cpp
    src/camera_ipc_client.cpp
)

target_include_directories(camera_service PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(camera_service PRIVATE
    protocol
    hal
    eipc
    pthread
)

target_compile_features(camera_service PRIVATE cxx_std_17)
target_compile_options(camera_service PRIVATE ${COMMON_COMPILE_OPTIONS})
