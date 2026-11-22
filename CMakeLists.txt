cmake_minimum_required(VERSION 3.16)
project(uav_camera VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "/oem/usr/lib:/usr/lib:/lib")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build uav camera test suite" ON)
option(BUILD_EXAMPLES "Build uav camera test suite" ON)

set(COMMON_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wcast-align
    -Wunused
)

message("Build for target: ${DEVICE_TARGET}")

# ==============================================================================
# Cross-compilation & Hardware Acceleration options
# ==============================================================================
if(CMAKE_CROSSCOMPILING OR DEVICE_TARGET STREQUAL "rockchip")
    set(HAVE_ROCKCHIP_MPP ON CACHE BOOL "Enable Rockchip MPP VPU hardware acceleration" FORCE)
    message(STATUS "[Platform] Cross-compiling for Rockchip ARM: HAVE_ROCKCHIP_MPP=ON")
else()
    option(HAVE_ROCKCHIP_MPP "Enable Rockchip MPP VPU hardware acceleration" OFF)
    message(STATUS "[Platform] Native Host build: HAVE_ROCKCHIP_MPP=${HAVE_ROCKCHIP_MPP}")
endif()

if(HAVE_ROCKCHIP_MPP)
    add_compile_definitions(HAVE_ROCKCHIP_MPP=1)
endif()

add_subdirectory(libs)
add_subdirectory(services)

