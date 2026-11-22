#ifndef __UAV_STREAMING_TYPES_HPP
#define __UAV_STREAMING_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/protocol.h"

namespace uav::streaming {

/* ========================================================================== */
/*                            Streaming Constants                             */
/* ========================================================================== */

inline constexpr std::string_view DEFAULT_SOCKET_PATH       = "/tmp/uav_eipc.sock";
inline constexpr uint16_t         DEFAULT_RTSP_PORT         = 8554;
inline constexpr std::string_view DEFAULT_MOUNT_POINT       = "/live/main";
inline constexpr uint32_t         DEFAULT_STREAM_WIDTH      = 1920;
inline constexpr uint32_t         DEFAULT_STREAM_HEIGHT     = 1080;
inline constexpr uint32_t         DEFAULT_STREAM_FPS        = 30;
inline constexpr uint8_t          DEFAULT_MAX_CLIENTS       = 4;

/* ========================================================================== */
/*                           Configuration Settings                           */
/* ========================================================================== */

/**
 * @brief Runtime configuration for StreamingService
 */
struct StreamingServiceConfig {
    std::string socket_path   = std::string(DEFAULT_SOCKET_PATH);
    uint16_t    rtsp_port     = DEFAULT_RTSP_PORT;
    std::string mount_point   = std::string(DEFAULT_MOUNT_POINT);
    uint32_t    width         = DEFAULT_STREAM_WIDTH;
    uint32_t    height        = DEFAULT_STREAM_HEIGHT;
    uint32_t    fps           = DEFAULT_STREAM_FPS;
    uint8_t     max_clients   = DEFAULT_MAX_CLIENTS;
    bool        auto_start    = true; /**< If true, RTSP server starts listening on launch */
};

} // namespace uav::streaming

#endif // __UAV_STREAMING_TYPES_HPP
