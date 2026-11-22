#ifndef __UAV_CAMERA_TYPES_HPP
#define __UAV_CAMERA_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/protocol.h"
#include "hal/hal_types.h"

namespace uav::camera {

/* ========================================================================== */
/*                             Camera Constants                               */
/* ========================================================================== */

inline constexpr std::string_view DEFAULT_CAMERA_DEVICE    = "/dev/video11";
inline constexpr std::string_view DEFAULT_SOCKET_PATH      = "/tmp/uav_eipc.sock";

inline constexpr uint32_t DEFAULT_CAMERA_WIDTH             = 1920;
inline constexpr uint32_t DEFAULT_CAMERA_HEIGHT            = 1080;
inline constexpr uint32_t DEFAULT_CAMERA_FPS               = 30;
inline constexpr uint32_t DEFAULT_CAMERA_BUFFERS           = 4;

/* ========================================================================== */
/*                           Configuration Settings                           */
/* ========================================================================== */

/**
 * @brief Runtime startup configuration for CameraService
 */
struct CameraServiceConfig {
    std::string        device_path  = std::string(DEFAULT_CAMERA_DEVICE);
    std::string        socket_path  = std::string(DEFAULT_SOCKET_PATH);
    uint32_t           width        = DEFAULT_CAMERA_WIDTH;
    uint32_t           height       = DEFAULT_CAMERA_HEIGHT;
    uint32_t           fps          = DEFAULT_CAMERA_FPS;
    hal_pixel_format_t pixel_format = HAL_PIX_FMT_NV12;
    uint32_t           buffer_count = DEFAULT_CAMERA_BUFFERS;
    bool               auto_start   = false; /**< If true, begins streaming immediately on boot */
};

} // namespace uav::camera

#endif // __UAV_CAMERA_TYPES_HPP
