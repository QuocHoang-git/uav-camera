cmake_minimum_required(VERSION 3.16)

add_executable(manager_service
    src/main.cpp
    src/app.cpp
    src/broker.cpp
    src/state_machine.cpp
    src/supervisor.cpp
    src/system_monitor.cpp
)

target_include_directories(manager_service PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(manager_service PRIVATE
    protocol
    eipc
    pthread
)

target_compile_features(manager_service PRIVATE cxx_std_17)
target_compile_options(manager_service PRIVATE ${COMMON_COMPILE_OPTIONS})
