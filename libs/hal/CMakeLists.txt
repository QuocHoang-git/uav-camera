cmake_minimum_required(VERSION 3.16)

add_library(hal STATIC
    src/rockchip/rk_camera.c
    src/rockchip/rk_encoder.c
)

target_include_directories(hal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(hal PRIVATE ${COMMON_COMPILE_OPTIONS} -Wno-pedantic)

# ==============================================================================
# Rockchip MPP Hardware VPU Integration
# ==============================================================================
if(HAVE_ROCKCHIP_MPP OR CMAKE_CROSSCOMPILING OR DEVICE_TARGET STREQUAL "rockchip")
    target_compile_definitions(hal PUBLIC HAVE_ROCKCHIP_MPP=1)

    if(DEFINED MPP_ROOT)
        if(EXISTS "${MPP_ROOT}/include")
            target_include_directories(hal PUBLIC "${MPP_ROOT}/include")
        endif()
        if(EXISTS "${MPP_ROOT}/include/rockchip")
            target_include_directories(hal PUBLIC "${MPP_ROOT}/include/rockchip")
        endif()
        if(EXISTS "${MPP_ROOT}/lib")
            target_link_directories(hal PUBLIC "${MPP_ROOT}/lib")
            target_link_libraries(hal PUBLIC rockchip_mpp)
        endif()
    elseif(CMAKE_CROSSCOMPILING)
        target_link_libraries(hal PUBLIC rockchip_mpp)
    endif()

    if(DEFINED RGA_ROOT)
        if(EXISTS "${RGA_ROOT}/include")
            target_include_directories(hal PUBLIC "${RGA_ROOT}/include")
        endif()
        if(EXISTS "${RGA_ROOT}/lib")
            target_link_directories(hal PUBLIC "${RGA_ROOT}/lib")
            target_link_libraries(hal PUBLIC rga)
        endif()
    endif()
endif()

if(BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/test_hal_encoder.c")
        add_executable(test_hal_encoder examples/test_hal_encoder.c)
        target_link_libraries(test_hal_encoder PRIVATE hal)
        target_compile_options(test_hal_encoder PRIVATE ${COMMON_COMPILE_OPTIONS})
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/test_hal_camera.c")
        add_executable(test_hal_camera examples/test_hal_camera.c)
        target_link_libraries(test_hal_camera PRIVATE hal)
        target_compile_options(test_hal_camera PRIVATE ${COMMON_COMPILE_OPTIONS})
    endif()
endif()
