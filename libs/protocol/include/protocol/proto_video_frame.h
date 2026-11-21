#ifndef __PROTO_VIDEO_FRAME_H
#define __PROTO_VIDEO_FRAME_H

#include "protocol/proto_types.h"
#include "protocol/proto_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                    Video Frame Command & Event IDs                         */
/* ========================================================================== */

/**
 * @brief Video Frame Streaming Message IDs (Range: 0x0300 - 0x03FF)
 */
enum {
    /* --- Frame Delivery (Zero-Copy with DMA-BUF FD passed via EIPC) --- */
    PROTO_EVENT_FRAME_RAW_AVAILABLE     = 0x0300, /**< New uncompressed sensor frame ready */
    PROTO_EVENT_FRAME_ENCODED_AVAILABLE = 0x0301, /**< New encoded VPU packet ready */

    /* --- Buffer Lifecycle & Flow Control --- */
    PROTO_CMD_FRAME_RELEASE             = 0x0310, /**< Consumer returns buffer back to producer */
    PROTO_RESP_FRAME_RELEASE            = 0x0311, /**< Producer acknowledges buffer recycle */

    /* --- Keyframe / IDR Requests --- */
    PROTO_CMD_FRAME_FORCE_IDR           = 0x0320, /**< Consumer requests immediate IDR/I-frame */
    PROTO_RESP_FRAME_FORCE_IDR          = 0x0321, /**< Producer IDR trigger ack */
};

/* ========================================================================== */
/*                             Enum Definitions                               */
/* ========================================================================== */

/**
 * @brief Video frame content classification
 */
typedef enum {
    PROTO_FRAME_TYPE_RAW            = 0x00, /**< Raw uncompressed pixel buffer (NV12/YUV/RGB) */
    PROTO_FRAME_TYPE_ENCODED_IDR    = 0x01, /**< Instantaneous Decoder Refresh / Keyframe */
    PROTO_FRAME_TYPE_ENCODED_P      = 0x02, /**< Predictive frame */
    PROTO_FRAME_TYPE_ENCODED_B      = 0x03, /**< Bi-directional predictive frame */
    PROTO_FRAME_TYPE_ENCODED_JPEG   = 0x04, /**< Still JPEG compressed frame */
} proto_frame_type_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Zero-copy video frame metadata payload
 *
 * NOTE: The underlying memory buffer is transferred as a Linux DMA-BUF file descriptor
 * via EIPC's native SCM_RIGHTS mechanism (eipc_message_t.fds[0]).
 * Receiver uses the FD to import directly into MPP / RGA / NPU / DRM without user-space copying.
 */
typedef struct {
    uint32_t width;             /**< Frame width in pixels */
    uint32_t height;            /**< Frame height in pixels */
    uint32_t stride_w;          /**< Horizontal byte stride (stride = width aligned) */
    uint32_t stride_h;          /**< Vertical line stride (height aligned) */
    uint32_t format;            /**< Pixel format (proto_pixel_format_t) or Codec */
    uint32_t size_bytes;        /**< Payload size in bytes within the DMA buffer */
    uint32_t buf_index;         /**< Ring buffer index allocated by producer */
    uint32_t sequence;          /**< Monotonically increasing frame counter */
    uint64_t timestamp_us;      /**< Sensor acquisition timestamp in microseconds */
    uint8_t  frame_type;        /**< proto_frame_type_t */
    uint8_t  is_keyframe;       /**< 1: Keyframe / IDR, 0: Non-keyframe */
    uint8_t  plane_count;       /**< Number of color planes (e.g. 2 for NV12) */
    uint8_t  reserved;          /**< Alignment padding (must be 0) */
} proto_video_frame_meta_t;

/**
 * @brief Payload for PROTO_CMD_FRAME_RELEASE
 *
 * Sent by the consumer (e.g. streaming_service or ai_service) back to the producer
 * (camera_service) once rendering/encoding/inference is complete.
 */
typedef struct {
    uint32_t buf_index;         /**< Ring buffer slot index to be returned */
    uint32_t sequence;          /**< Sequence number of the released frame */
} proto_frame_release_req_t;

/**
 * @brief Payload for PROTO_CMD_FRAME_FORCE_IDR
 *
 * Used when a new RTSP client connects to trigger an immediate I-frame for instant video start.
 */
typedef struct {
    uint8_t  stream_id;         /**< Identifier of the stream channel (e.g. 0: main, 1: sub) */
} proto_frame_force_idr_req_t;

#pragma pack(pop)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Convert frame type enum to readable string
 */
const char* proto_frame_type_str(proto_frame_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_VIDEO_FRAME_H */
