#ifndef __UAV_ENCODER_TYPES_HPP
#define __UAV_ENCODER_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "protocol/protocol.h"
#include "hal/hal_types.h"
#include "hal/hal_encoder.h"

namespace uav::encoder {

/* ========================================================================== */
/*                             Encoder Constants                              */
/* ========================================================================== */

inline constexpr std::string_view DEFAULT_SOCKET_PATH       = "/tmp/uav_eipc.sock";
inline constexpr uint32_t         DEFAULT_ENC_WIDTH         = 1920;
inline constexpr uint32_t         DEFAULT_ENC_HEIGHT        = 1080;
inline constexpr uint32_t         DEFAULT_ENC_FPS           = 30;
inline constexpr uint32_t         DEFAULT_ENC_BITRATE_KBPS  = 4000;
inline constexpr uint32_t         DEFAULT_ENC_GOP           = 30;

/* ========================================================================== */
/*                           Configuration Settings                           */
/* ========================================================================== */

/**
 * @brief Runtime startup configuration for EncoderService
 */
struct EncoderServiceConfig {
    std::string        socket_path        = std::string(DEFAULT_SOCKET_PATH);
    hal_codec_type_t   codec              = HAL_CODEC_H264;
    uint32_t           width              = DEFAULT_ENC_WIDTH;
    uint32_t           height             = DEFAULT_ENC_HEIGHT;
    uint32_t           fps                = DEFAULT_ENC_FPS;
    uint32_t           bitrate_kbps       = DEFAULT_ENC_BITRATE_KBPS;
    uint32_t           gop_size           = DEFAULT_ENC_GOP;
    hal_pixel_format_t input_format       = HAL_PIX_FMT_NV12;
    int                rc_mode            = 0; /* 0: CBR, 1: VBR */
    int                profile            = 1; /* 0: Baseline, 1: Main, 2: High */
    proto_service_id_t downstream_service = PROTO_SVC_STREAMING;
    bool               auto_start         = true; /**< If true, VPU encoder starts immediately */
};

} // namespace uav::encoder

#endif // __UAV_ENCODER_TYPES_HPP
