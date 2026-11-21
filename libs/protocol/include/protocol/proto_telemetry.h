#ifndef __PROTO_TELEMETRY_H
#define __PROTO_TELEMETRY_H

#include "protocol/proto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                      Telemetry Command & Event IDs                         */
/* ========================================================================== */

/**
 * @brief Telemetry & MAVLink Message IDs (Range: 0x0500 - 0x05FF)
 */
enum {
    /* --- Vehicle Metrics Broadcasts (Periodic) --- */
    PROTO_EVENT_TELEM_ATTITUDE          = 0x0500, /**< Vehicle & gimbal orientation (roll/pitch/yaw) */
    PROTO_EVENT_TELEM_GPS               = 0x0501, /**< Global positioning & altitude for Geotagging */
    PROTO_EVENT_TELEM_BATTERY           = 0x0502, /**< Battery voltage, current, and capacity */
    PROTO_EVENT_TELEM_FLIGHT_STATE      = 0x0503, /**< Arming state and active flight mode */
    PROTO_EVENT_TELEM_LINK_STATUS       = 0x0504, /**< MAVLink / FC connection health */

    /* --- MAVLink Ground Control Camera Commands --- */
    PROTO_CMD_TELEM_TRIGGER_PHOTO       = 0x0510, /**< QGC / MAVLink capture still photo trigger */
    PROTO_RESP_TELEM_TRIGGER_PHOTO      = 0x0511, /**< Photo trigger ack */
    PROTO_CMD_TELEM_TRIGGER_RECORD      = 0x0512, /**< QGC / MAVLink start/stop video recording */
    PROTO_RESP_TELEM_TRIGGER_RECORD     = 0x0513, /**< Record trigger ack */
    PROTO_CMD_TELEM_SET_GIMBAL_TARGET   = 0x0520, /**< Target gimbal pitch/roll/yaw angle */
    PROTO_RESP_TELEM_SET_GIMBAL_TARGET  = 0x0521, /**< Gimbal target ack */
    PROTO_CMD_TELEM_SET_ZOOM            = 0x0522, /**< Set digital / optical zoom factor */
    PROTO_RESP_TELEM_SET_ZOOM           = 0x0523, /**< Zoom factor ack */
};

/* ========================================================================== */
/*                             Enum Definitions                               */
/* ========================================================================== */

/**
 * @brief Flight controller connection state
 */
typedef enum {
    PROTO_FC_LINK_DISCONNECTED  = 0x00,
    PROTO_FC_LINK_CONNECTED     = 0x01,
    PROTO_FC_LINK_LOST          = 0x02,
} proto_fc_link_state_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Vehicle & Gimbal attitude payload (for OSD overlay & EIS)
 */
typedef struct {
    float    roll_deg;          /**< Roll angle in degrees (-180.0 to +180.0) */
    float    pitch_deg;         /**< Pitch angle in degrees (-90.0 to +90.0) */
    float    yaw_deg;           /**< Yaw / Heading angle in degrees (0.0 to 360.0) */
    float    gimbal_pitch_deg;  /**< Current physical gimbal pitch angle */
    float    gimbal_yaw_deg;    /**< Current physical gimbal yaw angle */
    uint64_t timestamp_us;      /**< Monotonic timestamp in microseconds */
} proto_telem_attitude_event_t;

/**
 * @brief GPS position payload (for EXIF Geotagging and Subtitles)
 */
typedef struct {
    int32_t  latitude_deg_e7;   /**< Latitude in degrees * 1e7 */
    int32_t  longitude_deg_e7;  /**< Longitude in degrees * 1e7 */
    float    alt_msl_m;         /**< Altitude Above Mean Sea Level in meters */
    float    alt_relative_m;    /**< Altitude above home / takeoff position */
    float    ground_speed_mps;  /**< Ground speed in meters/second */
    float    heading_deg;       /**< GPS heading in degrees */
    uint8_t  satellites_visible;/**< Number of locked GNSS satellites */
    uint8_t  fix_type;          /**< 0: No fix, 2: 2D, 3: 3D, 4: DGPS, 5: RTK */
    uint64_t timestamp_us;      /**< GPS lock timestamp */
} proto_telem_gps_event_t;

/**
 * @brief Battery status payload (for low-power failsafe recording shutdown)
 */
typedef struct {
    uint16_t voltage_mv;        /**< Total battery voltage in millivolts (e.g. 15200 for 15.2V) */
    int16_t  current_ca;        /**< Battery current draw in centi-amperes (100 = 1.0A) */
    uint8_t  remaining_pct;     /**< Remaining battery capacity percentage (0 - 100%) */
    uint8_t  is_critical;       /**< 1: Battery level critically low, failsafe triggered */
} proto_telem_battery_event_t;

/**
 * @brief Flight status payload
 */
typedef struct {
    uint8_t  is_armed;          /**< 1: Motors armed / flying, 0: Disarmed */
    uint8_t  flight_mode;       /**< 0: Manual, 1: AltHold, 2: PosHold, 3: Mission, 4: RTL */
    uint8_t  landed_state;      /**< 0: On ground, 1: In air, 2: Landing */
    uint8_t  reserved;
} proto_telem_flight_state_event_t;

/**
 * @brief MAVLink Link Health payload
 */
typedef struct {
    uint8_t  link_state;        /**< proto_fc_link_state_t */
    uint8_t  link_quality_pct;  /**< Radio / Serial packet success rate (0 - 100%) */
    uint16_t packets_received;  /**< MAVLink message count received */
    uint16_t packets_lost;      /**< MAVLink dropped packet count */
} proto_telem_link_status_event_t;

/**
 * @brief Payload for PROTO_CMD_TELEM_TRIGGER_RECORD
 */
typedef struct {
    uint8_t  action;            /**< 1: Start recording, 0: Stop recording */
} proto_telem_trigger_record_req_t;

/**
 * @brief Payload for PROTO_CMD_TELEM_SET_GIMBAL_TARGET
 */
typedef struct {
    float    target_pitch_deg;  /**< Target pitch angle (-90 to +30 deg) */
    float    target_yaw_deg;    /**< Target yaw angle (-180 to +180 deg) */
} proto_telem_gimbal_target_req_t;

/**
 * @brief Payload for PROTO_CMD_TELEM_SET_ZOOM
 */
typedef struct {
    float    zoom_factor;       /**< Magnification factor (1.0 = 1x, 2.0 = 2x, etc.) */
} proto_telem_set_zoom_req_t;

#pragma pack(pop)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Convert FC link state enum to readable string
 */
const char* proto_fc_link_state_str(proto_fc_link_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_TELEMETRY_H */
