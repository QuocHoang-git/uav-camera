#ifndef __PROTO_TYPES_H
#define __PROTO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                             Protocol Constants                             */
/* ========================================================================== */

#define PROTO_VERSION_MAJOR        (1)
#define PROTO_VERSION_MINOR        (0)
#define PROTO_VERSION_CURRENT      (PROTO_VERSION_MAJOR)

/**
 * @brief Max payload size in bytes (constrained by EIPC maximum payload)
 */
#define PROTO_MAX_PAYLOAD_SIZE     (1024)

/* ========================================================================== */
/*                            Service Identifiers                             */
/* ========================================================================== */

/**
 * @brief Unique identifier for each service / daemon in the UAV system
 */
typedef enum {
    PROTO_SVC_UNKNOWN       = 0x00,
    PROTO_SVC_BROKER        = 0x01, /**< EIPC Broker daemon (connection arbiter) */
    PROTO_SVC_MANAGER       = 0x02, /**< System manager & state machine supervisor */
    PROTO_SVC_CAMERA        = 0x03, /**< Camera HAL, capture & VPU encoder */
    PROTO_SVC_STREAMING     = 0x04, /**< RTSP / WebRTC streaming distribution */
    PROTO_SVC_TELEMETRY     = 0x05, /**< MAVLink telemetry bridge & vehicle metrics */
    PROTO_SVC_AI            = 0x06, /**< NPU inference & computer vision service */
    PROTO_SVC_STORAGE       = 0x07, /**< Media recording & file management */
    
    PROTO_SVC_BROADCAST     = 0xFF  /**< Broadcast destination to all subscribers */
} proto_service_id_t;

/* ========================================================================== */
/*                            Message Categories                              */
/* ========================================================================== */

/**
 * @brief IPC Message semantics
 */
typedef enum {
    PROTO_MSG_TYPE_UNKNOWN  = 0x00,
    PROTO_MSG_TYPE_CMD      = 0x01, /**< Command / Request: sender expects a response */
    PROTO_MSG_TYPE_RESP     = 0x02, /**< Response: reply to a command with matching trans_id */
    PROTO_MSG_TYPE_EVENT    = 0x03, /**< Asynchronous Event / Notification: one-way publish */
} proto_msg_type_t;

/* ========================================================================== */
/*                            Common Status Codes                             */
/* ========================================================================== */

/**
 * @brief Status returned in Response payloads or API calls
 */
typedef enum {
    PROTO_OK                 =  0,  /**< Operation successful */
    PROTO_ERR_GENERIC        = -1,  /**< Unspecified generic error */
    PROTO_ERR_INVALID_ARG    = -2,  /**< Invalid parameter or null pointer */
    PROTO_ERR_NO_MEM         = -3,  /**< Out of memory */
    PROTO_ERR_BUSY           = -4,  /**< Device or service is busy */
    PROTO_ERR_TIMEOUT        = -5,  /**< Request timed out */
    PROTO_ERR_NOT_SUPPORTED  = -6,  /**< Feature or command not supported */
    PROTO_ERR_NOT_PERMITTED  = -7,  /**< Permission denied or invalid state */
    PROTO_ERR_NOT_FOUND      = -8,  /**< Resource or device not found */
    PROTO_ERR_IO             = -9,  /**< Hardware / Socket I/O error */
    PROTO_ERR_BAD_PAYLOAD    = -10, /**< Malformed payload or size mismatch */
    PROTO_ERR_SERVICE_DOWN   = -11, /**< Target service is unavailable */
} proto_status_t;

/* ========================================================================== */
/*                      Lean 8-Byte Protocol Header Format                    */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Ultra-compact 8-byte application header for IPC messages over EIPC
 *
 * Binary layout (Total 8 bytes, fits directly into a 64-bit CPU register word):
 * [0]     version     (0x01: Application Protocol Version)
 * [1]     msg_type    (proto_msg_type_t: CMD / RESP / EVENT)
 * [2]     src_id      (proto_service_id_t: Source service)
 * [3]     dst_id      (proto_service_id_t: Destination service or BROADCAST)
 * [4..5]  msg_id      (Specific Command ID or Event ID)
 * [6..7]  trans_id    (Transaction ID for Request-Reply matching)
 *
 * Note:
 * - Frame synchronization (Magic) and CRC checks are offloaded to lower-layer EIPC.
 * - Payload length is derived from: (eipc_message_t.size - sizeof(proto_header_t)).
 * - Status code is carried inside the response payload (for RESP type).
 */
typedef struct {
    uint8_t  version;       /**< PROTO_VERSION_CURRENT */
    uint8_t  msg_type;      /**< proto_msg_type_t */
    uint8_t  src_id;        /**< proto_service_id_t (sender) */
    uint8_t  dst_id;        /**< proto_service_id_t (receiver) */
    uint16_t msg_id;        /**< Specific command or event identifier */
    uint16_t trans_id;      /**< Transaction ID for request-reply correlation */
} proto_header_t;

/**
 * @brief Standard minimal response payload for commands that only return a status
 */
typedef struct {
    int32_t status;         /**< proto_status_t error code */
} proto_resp_status_t;

#pragma pack(pop)

#define PROTO_HEADER_SIZE          (sizeof(proto_header_t)) /* Exactly 8 bytes */
#define PROTO_MAX_PACKET_SIZE      (PROTO_HEADER_SIZE + PROTO_MAX_PAYLOAD_SIZE)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Initialize a protocol header with standard defaults
 */
static inline void proto_header_init(proto_header_t *hdr,
                                     proto_msg_type_t type,
                                     proto_service_id_t src,
                                     proto_service_id_t dst,
                                     uint16_t msg_id,
                                     uint16_t trans_id) {
    if (!hdr) return;
    hdr->version  = PROTO_VERSION_CURRENT;
    hdr->msg_type = (uint8_t)type;
    hdr->src_id   = (uint8_t)src;
    hdr->dst_id   = (uint8_t)dst;
    hdr->msg_id   = msg_id;
    hdr->trans_id = trans_id;
}

/**
 * @brief Validate version and basic header constraints
 */
static inline bool proto_header_is_valid(const proto_header_t *hdr) {
    if (!hdr) return false;
    return (hdr->version == PROTO_VERSION_CURRENT) &&
           (hdr->msg_type >= PROTO_MSG_TYPE_CMD && hdr->msg_type <= PROTO_MSG_TYPE_EVENT);
}

/**
 * @brief Helper to calculate payload size from total received EIPC size
 */
static inline size_t proto_payload_len(size_t total_eipc_size) {
    return (total_eipc_size >= PROTO_HEADER_SIZE) ? (total_eipc_size - PROTO_HEADER_SIZE) : 0;
}

/**
 * @brief Convert status code to readable string
 */
const char* proto_status_str(proto_status_t status);

/**
 * @brief Convert service ID to name string
 */
const char* proto_service_name(proto_service_id_t svc_id);

/**
 * @brief Convert message type to name string
 */
const char* proto_msg_type_str(proto_msg_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_TYPES_H */
