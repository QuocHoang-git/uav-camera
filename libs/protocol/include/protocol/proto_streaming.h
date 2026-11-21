#ifndef __PROTO_STREAMING_H
#define __PROTO_STREAMING_H

#include "protocol/proto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                      Streaming Command & Event IDs                         */
/* ========================================================================== */

/**
 * @brief Streaming Message IDs (Range: 0x0400 - 0x04FF)
 */
enum {
    /* --- Stream Server Lifecycle --- */
    PROTO_CMD_STREAM_START              = 0x0400, /**< Start RTSP/WebRTC distribution server */
    PROTO_RESP_STREAM_START             = 0x0401, /**< Start stream server ack */
    PROTO_CMD_STREAM_STOP               = 0x0402, /**< Stop distribution server */
    PROTO_RESP_STREAM_STOP              = 0x0403, /**< Stop stream server ack */

    /* --- Configuration --- */
    PROTO_CMD_STREAM_SET_CONFIG         = 0x0410, /**< Set network port, path, and transport */
    PROTO_RESP_STREAM_SET_CONFIG        = 0x0411, /**< Set stream config ack */
    PROTO_CMD_STREAM_GET_CONFIG         = 0x0412, /**< Query active streaming config & URL */
    PROTO_RESP_STREAM_GET_CONFIG        = 0x0413, /**< Active streaming config reply */

    /* --- Client Connection Events --- */
    PROTO_EVENT_STREAM_CLIENT_CONNECTED = 0x0420, /**< Notification: client joined (e.g. QGC) */
    PROTO_EVENT_STREAM_CLIENT_DISCONNECTED=0x0421,/**< Notification: client departed */
    PROTO_EVENT_STREAM_STATS            = 0x0422, /**< Periodic network distribution stats */

    /* --- Status Query --- */
    PROTO_CMD_STREAM_GET_STATUS         = 0x0430, /**< Query server state and active sessions */
    PROTO_RESP_STREAM_GET_STATUS        = 0x0431, /**< Streaming status reply */
};

/* ========================================================================== */
/*                             Enum Definitions                               */
/* ========================================================================== */

/**
 * @brief Streaming transport protocol
 */
typedef enum {
    PROTO_STREAM_PROTO_RTSP_UDP         = 0x00, /**< RTSP with RTP over UDP */
    PROTO_STREAM_PROTO_RTSP_TCP         = 0x01, /**< RTSP with RTP interleaved over TCP */
    PROTO_STREAM_PROTO_WEBRTC           = 0x02, /**< Ultra-low-latency WebRTC */
} proto_stream_proto_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Streaming server network configuration
 */
typedef struct {
    uint16_t port;              /**< RTSP listening port (default: 554 or 8554) */
    uint8_t  protocol;          /**< proto_stream_proto_t */
    uint8_t  max_clients;       /**< Maximum concurrent viewer sessions (e.g. 4) */
    char     mount_point[32];   /**< URI path, e.g. "/live/ch0" */
    char     auth_username[16]; /**< Optional username (empty for anonymous) */
    char     auth_password[16]; /**< Optional password */
} proto_stream_config_t;

/**
 * @brief Payload for PROTO_RESP_STREAM_GET_CONFIG
 */
typedef struct {
    int32_t               status;       /**< proto_status_t */
    proto_stream_config_t config;       /**< Active configuration */
    char                  stream_url[64];/**< Full published URL, e.g. "rtsp://0.0.0.0:8554/live/ch0" */
} proto_stream_get_config_resp_t;

/**
 * @brief Payload for PROTO_EVENT_STREAM_CLIENT_CONNECTED & PROTO_EVENT_STREAM_CLIENT_DISCONNECTED
 */
typedef struct {
    uint32_t client_id;         /**< Internal session identifier */
    char     client_ip[16];     /**< IPv4 string representation, e.g. "192.168.1.100" */
    uint16_t client_port;       /**< Remote port */
    uint8_t  active_clients;    /**< Total number of currently connected viewers */
} proto_stream_client_event_t;

/**
 * @brief Payload for PROTO_EVENT_STREAM_STATS
 */
typedef struct {
    uint8_t  active_clients;    /**< Current active viewer count */
    uint32_t outgoing_kbps;     /**< Total outgoing network throughput in Kbps */
    float    packet_loss_pct;   /**< Estimated packet loss percentage */
    uint64_t total_bytes_sent;  /**< Cumulative bytes sent since server start */
} proto_stream_stats_event_t;

/**
 * @brief Payload for PROTO_RESP_STREAM_GET_STATUS
 */
typedef struct {
    int32_t  status;            /**< proto_status_t */
    uint8_t  is_running;        /**< 1: Server active and listening, 0: Stopped */
    uint8_t  active_clients;    /**< Number of connected clients */
    uint32_t uptime_sec;        /**< Seconds streaming server has been online */
    char     stream_url[64];    /**< Active RTSP URL */
} proto_stream_status_resp_t;

#pragma pack(pop)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Convert streaming protocol enum to readable string
 */
const char* proto_stream_proto_str(proto_stream_proto_t proto);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_STREAMING_H */
