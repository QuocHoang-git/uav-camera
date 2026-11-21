#ifndef __PROTO_SYSTEM_H
#define __PROTO_SYSTEM_H

#include "protocol/proto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                       System Command & Event IDs                           */
/* ========================================================================== */

/**
 * @brief System Message IDs (Range: 0x0100 - 0x01FF)
 */
enum {
    /* --- Ping / Heartbeat --- */
    PROTO_CMD_SYS_PING                  = 0x0100, /**< Request keep-alive ping */
    PROTO_RESP_SYS_PONG                 = 0x0101, /**< Reply pong */

    /* --- Service Lifecycle Registration --- */
    PROTO_CMD_SYS_REGISTER              = 0x0110, /**< Client announces itself to Manager */
    PROTO_RESP_SYS_REGISTER             = 0x0111, /**< Manager acknowledges registration */
    PROTO_CMD_SYS_UNREGISTER            = 0x0112, /**< Client gracefully disconnects */
    PROTO_RESP_SYS_UNREGISTER           = 0x0113, /**< Acknowledge disconnect */
    PROTO_EVENT_SYS_SERVICE_STATUS      = 0x0114, /**< Notification: service up/down */

    /* --- System State Machine (FSM) --- */
    PROTO_CMD_SYS_GET_STATE             = 0x0120, /**< Query current system state */
    PROTO_RESP_SYS_GET_STATE            = 0x0121, /**< Current system state reply */
    PROTO_CMD_SYS_SET_STATE             = 0x0122, /**< Request system state transition */
    PROTO_RESP_SYS_SET_STATE            = 0x0123, /**< State transition result */
    PROTO_EVENT_SYS_STATE_CHANGED       = 0x0124, /**< Broadcast: state changed */

    /* --- Hardware / Resource Metrics --- */
    PROTO_CMD_SYS_GET_METRICS           = 0x0130, /**< Query CPU/Memory/Temperature */
    PROTO_RESP_SYS_GET_METRICS          = 0x0131, /**< Metrics data reply */

    /* --- System Power / Maintenance --- */
    PROTO_CMD_SYS_REBOOT                = 0x0140, /**< Request system reboot */
    PROTO_RESP_SYS_REBOOT               = 0x0141, /**< Reboot ack */

    /* --- EIPC Direct Link Request --- */
    PROTO_CMD_SYS_REQUEST_DIRECT_LINK   = 0x0150, /**< Request direct P2P link between two peers */
    PROTO_RESP_SYS_REQUEST_DIRECT_LINK  = 0x0151, /**< Direct link response with FD passed */
};

/* ========================================================================== */
/*                           System States (FSM)                              */
/* ========================================================================== */

/**
 * @brief High-level UAV vision payload operating states
 */
typedef enum {
    SYS_STATE_UNKNOWN       = 0x00,
    SYS_STATE_INIT          = 0x01, /**< Initializing hardware & daemons */
    SYS_STATE_STANDBY       = 0x02, /**< Ready, idle, no active stream/record */
    SYS_STATE_STREAMING     = 0x03, /**< Active RTSP/WebRTC video streaming */
    SYS_STATE_RECORDING     = 0x04, /**< Active local storage recording */
    SYS_STATE_STREAM_RECORD = 0x05, /**< Concurrent streaming and recording */
    SYS_STATE_CALIBRATION   = 0x06, /**< Sensor/gimbal calibration mode */
    SYS_STATE_LOW_POWER     = 0x07, /**< Battery critical or low power standby */
    SYS_STATE_ERROR         = 0x08, /**< Fault detected (sensor or storage error) */
    SYS_STATE_SHUTDOWN      = 0x09, /**< Performing safe shutdown/unmount */
} proto_sys_state_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Payload for PROTO_CMD_SYS_PING & PROTO_RESP_SYS_PONG
 */
typedef struct {
    uint64_t timestamp_ms;      /**< Monotonic timestamp in milliseconds */
    uint32_t seq;               /**< Ping sequence number */
} proto_sys_ping_pong_t;

/**
 * @brief Payload for PROTO_CMD_SYS_REGISTER
 */
typedef struct {
    uint8_t  service_id;        /**< proto_service_id_t of the registering client */
    uint32_t pid;               /**< Linux PID of the service process */
    uint16_t version_major;     /**< Service major version */
    uint16_t version_minor;     /**< Service minor version */
    char     service_name[16];  /**< Human-readable null-terminated name, e.g. "cam_srv" */
} proto_sys_register_req_t;

/**
 * @brief Payload for PROTO_RESP_SYS_REGISTER
 */
typedef struct {
    int32_t  status;            /**< PROTO_OK or error code */
    uint8_t  assigned_id;       /**< Confirmed service ID */
    uint8_t  current_sys_state; /**< Current proto_sys_state_t of the system */
} proto_sys_register_resp_t;

/**
 * @brief Payload for PROTO_EVENT_SYS_SERVICE_STATUS
 */
typedef struct {
    uint8_t  service_id;        /**< proto_service_id_t */
    uint8_t  is_online;         /**< 1: Connected & Ready, 0: Disconnected / Crashed */
    uint32_t pid;               /**< Process ID */
} proto_sys_service_status_event_t;

/**
 * @brief Payload for PROTO_RESP_SYS_GET_STATE
 */
typedef struct {
    int32_t  status;            /**< proto_status_t */
    uint8_t  current_state;     /**< proto_sys_state_t */
    uint8_t  previous_state;    /**< proto_sys_state_t */
    uint32_t state_uptime_sec;  /**< Seconds spent in current state */
} proto_sys_get_state_resp_t;

/**
 * @brief Payload for PROTO_CMD_SYS_SET_STATE
 */
typedef struct {
    uint8_t  target_state;      /**< Desired proto_sys_state_t */
    uint8_t  force;             /**< 1: Force transition even if warnings exist */
    uint16_t reason_code;       /**< Optional caller reason */
} proto_sys_set_state_req_t;

/**
 * @brief Payload for PROTO_EVENT_SYS_STATE_CHANGED
 */
typedef struct {
    uint8_t  old_state;         /**< proto_sys_state_t */
    uint8_t  new_state;         /**< proto_sys_state_t */
    uint16_t reason_code;       /**< Reason identifier */
    uint64_t timestamp_ms;      /**< Monotonic time at transition */
} proto_sys_state_changed_event_t;

/**
 * @brief Payload for PROTO_RESP_SYS_GET_METRICS (SoC health)
 */
typedef struct {
    int32_t  status;            /**< proto_status_t */
    int16_t  cpu_temp_c;        /**< CPU temperature in Celsius (e.g. 55) */
    uint8_t  cpu_usage_pct;     /**< CPU load percentage (0 - 100%) */
    uint8_t  mem_usage_pct;     /**< RAM usage percentage (0 - 100%) */
    uint32_t mem_free_kb;       /**< Free RAM in Kilobytes */
    uint32_t storage_free_mb;   /**< Free space on recording media (MB) */
    uint32_t uptime_sec;        /**< System total uptime in seconds */
} proto_sys_metrics_resp_t;

/**
 * @brief Payload for PROTO_CMD_SYS_REQUEST_DIRECT_LINK
 *
 * Requests the Broker to create a socketpair and pass the direct file descriptor
 * to both target_service_id and the caller, establishing zero-copy P2P bypass.
 */
typedef struct {
    uint8_t  target_service_id; /**< proto_service_id_t of the peer to connect with */
} proto_sys_direct_link_req_t;

#pragma pack(pop)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Convert system state enum to readable string
 */
const char* proto_sys_state_str(proto_sys_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_SYSTEM_H */
