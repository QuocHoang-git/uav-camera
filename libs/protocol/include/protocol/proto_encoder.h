#ifndef __PROTO_ENCODER_H
#define __PROTO_ENCODER_H

#include "protocol/proto_types.h"
#include "protocol/proto_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                      Encoder Command & Event IDs                           */
/* ========================================================================== */

/**
 * @brief Hardware VPU Video Encoder Message IDs (Range: 0x0600 - 0x06FF)
 */
enum {
    /* --- Encoder Lifecycle --- */
    PROTO_CMD_ENC_START                 = 0x0600, /**< Start hardware VPU encoder pipeline */
    PROTO_RESP_ENC_START                = 0x0601, /**< Start encoder ack */
    PROTO_CMD_ENC_STOP                  = 0x0602, /**< Stop VPU encoder */
    PROTO_RESP_ENC_STOP                 = 0x0603, /**< Stop encoder ack */

    /* --- Configuration --- */
    PROTO_CMD_ENC_SET_CONFIG            = 0x0610, /**< Set codec, resolution, bitrate, GOP */
    PROTO_RESP_ENC_SET_CONFIG           = 0x0611, /**< Set config ack */
    PROTO_CMD_ENC_GET_CONFIG            = 0x0612, /**< Query active encoder configuration */
    PROTO_RESP_ENC_GET_CONFIG           = 0x0613, /**< Active encoder configuration reply */

    /* --- Dynamic Rate Control (Live Bitrate Adaptation) --- */
    PROTO_CMD_ENC_SET_BITRATE           = 0x0620, /**< Change target bitrate dynamically on-the-fly */
    PROTO_RESP_ENC_SET_BITRATE          = 0x0621, /**< Bitrate change ack */

    /* --- Encoder Status & Metrics --- */
    PROTO_CMD_ENC_GET_STATUS            = 0x0630, /**< Query VPU encoder operational metrics */
    PROTO_RESP_ENC_GET_STATUS           = 0x0631, /**< Encoder metrics reply */
};

/* ========================================================================== */
/*                             Enum Definitions                               */
/* ========================================================================== */

/**
 * @brief Rate control algorithm mode
 */
typedef enum {
    PROTO_RC_CBR                        = 0, /**< Constant Bitrate (optimal for radio telemetry) */
    PROTO_RC_VBR                        = 1, /**< Variable Bitrate (optimal for storage saving) */
} proto_rc_mode_t;

/**
 * @brief H.264 / H.265 encoding profile
 */
typedef enum {
    PROTO_ENC_PROFILE_BASELINE          = 0, /**< Lowest complexity & latency */
    PROTO_ENC_PROFILE_MAIN              = 1, /**< Balanced quality & compression */
    PROTO_ENC_PROFILE_HIGH              = 2, /**< Maximum quality for recording */
} proto_enc_profile_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Encoder configuration parameters
 */
typedef struct {
    uint32_t width;             /**< Frame width in pixels */
    uint32_t height;            /**< Frame height in pixels */
    uint32_t fps;               /**< Frame rate */
    uint32_t bitrate_kbps;      /**< Target bitrate in Kbps (e.g. 4000) */
    uint8_t  codec;             /**< proto_codec_type_t (PROTO_CODEC_H264 / H265) */
    uint8_t  rc_mode;           /**< proto_rc_mode_t (CBR / VBR) */
    uint8_t  profile;           /**< proto_enc_profile_t */
    uint8_t  gop_size;          /**< I-frame interval in frames (e.g. 30) */
} proto_enc_config_t;

/**
 * @brief Payload for PROTO_RESP_ENC_GET_CONFIG
 */
typedef struct {
    int32_t            status;  /**< proto_status_t */
    proto_enc_config_t config;  /**< Active encoder parameters */
} proto_enc_get_config_resp_t;

/**
 * @brief Payload for PROTO_CMD_ENC_SET_BITRATE (On-the-fly adjustment)
 */
typedef struct {
    uint32_t target_bitrate_kbps; /**< New bitrate in Kbps */
} proto_enc_set_bitrate_req_t;

/**
 * @brief Payload for PROTO_RESP_ENC_GET_STATUS
 */
typedef struct {
    int32_t  status;            /**< proto_status_t */
    uint8_t  is_encoding;       /**< 1: Active, 0: Idle/Stopped */
    uint32_t encoded_frames;    /**< Total frames successfully compressed */
    uint32_t dropped_frames;    /**< Frames dropped due to VPU queue full */
    float    output_fps;        /**< Current measured output FPS */
    uint32_t current_bitrate_kbps;/**< Current measured output bitrate in Kbps */
    uint8_t  vpu_load_pct;      /**< Estimated VPU hardware utilization % */
} proto_enc_status_resp_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_ENCODER_H */
