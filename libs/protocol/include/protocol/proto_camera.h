#ifndef __PROTO_CAMERA_H
#define __PROTO_CAMERA_H

#include "protocol/proto_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                       Camera Command & Event IDs                           */
/* ========================================================================== */

/**
 * @brief Camera Message IDs (Range: 0x0200 - 0x02FF)
 */
enum {
    /* --- Stream Acquisition Control --- */
    PROTO_CMD_CAM_START                 = 0x0200, /**< Start sensor capture & pipeline */
    PROTO_RESP_CAM_START                = 0x0201, /**< Start capture ack */
    PROTO_CMD_CAM_STOP                  = 0x0202, /**< Stop capture */
    PROTO_RESP_CAM_STOP                 = 0x0203, /**< Stop capture ack */

    /* --- Configuration --- */
    PROTO_CMD_CAM_SET_CONFIG            = 0x0210, /**< Set sensor & encoder parameters */
    PROTO_RESP_CAM_SET_CONFIG           = 0x0211, /**< Set config ack */
    PROTO_CMD_CAM_GET_CONFIG            = 0x0212, /**< Query current configuration */
    PROTO_RESP_CAM_GET_CONFIG           = 0x0213, /**< Current configuration reply */

    /* --- Still Photo Capture --- */
    PROTO_CMD_CAM_TAKE_PHOTO            = 0x0220, /**< Trigger still photo snapshot */
    PROTO_RESP_CAM_TAKE_PHOTO           = 0x0221, /**< Snapshot trigger accepted */
    PROTO_EVENT_CAM_PHOTO_TAKEN         = 0x0222, /**< Notification: photo saved to storage */

    /* --- Video Recording Control --- */
    PROTO_CMD_CAM_START_RECORD          = 0x0230, /**< Start local video recording */
    PROTO_RESP_CAM_START_RECORD         = 0x0231, /**< Start recording ack */
    PROTO_CMD_CAM_STOP_RECORD           = 0x0232, /**< Stop local video recording */
    PROTO_RESP_CAM_STOP_RECORD          = 0x0233, /**< Stop recording ack */
    PROTO_EVENT_CAM_RECORD_STATUS       = 0x0234, /**< Periodic recording status update */

    /* --- Sensor & ISP Controls --- */
    PROTO_CMD_CAM_SET_ISP_PARAMS        = 0x0240, /**< Adjust exposure, gain, white balance */
    PROTO_RESP_CAM_SET_ISP_PARAMS       = 0x0241, /**< ISP adjust ack */
    PROTO_CMD_CAM_GET_ISP_PARAMS        = 0x0242, /**< Query ISP parameters */
    PROTO_RESP_CAM_GET_ISP_PARAMS       = 0x0243, /**< ISP parameters reply */

    /* --- Camera Health & Status --- */
    PROTO_CMD_CAM_GET_STATUS            = 0x0250, /**< Query operational status */
    PROTO_RESP_CAM_GET_STATUS           = 0x0251, /**< Operational status reply */
};

/* ========================================================================== */
/*                             Enum Definitions                               */
/* ========================================================================== */

/**
 * @brief Video codec selection
 */
typedef enum {
    PROTO_CODEC_H264    = 0,
    PROTO_CODEC_H265    = 1,
    PROTO_CODEC_JPEG    = 2,
} proto_codec_type_t;

/**
 * @brief Pixel format definition
 */
typedef enum {
    PROTO_PIX_FMT_NV12  = 0,
    PROTO_PIX_FMT_NV21  = 1,
    PROTO_PIX_FMT_YUYV  = 2,
    PROTO_PIX_FMT_RGB24 = 3,
    PROTO_PIX_FMT_BGR24 = 4,
} proto_pixel_format_t;

/**
 * @brief Exposure control mode
 */
typedef enum {
    PROTO_EXPOSURE_AUTO     = 0,
    PROTO_EXPOSURE_MANUAL   = 1,
} proto_exposure_mode_t;

/**
 * @brief White balance mode
 */
typedef enum {
    PROTO_WB_AUTO           = 0,
    PROTO_WB_MANUAL         = 1,
    PROTO_WB_DAYLIGHT       = 2,
    PROTO_WB_CLOUDY         = 3,
} proto_wb_mode_t;

/* ========================================================================== */
/*                             Payload Structures                             */
/* ========================================================================== */

#pragma pack(push, 1)

/**
 * @brief Video streaming / sensor configuration
 */
typedef struct {
    uint32_t width;             /**< Frame width in pixels (e.g. 1920, 1280, 2304) */
    uint32_t height;            /**< Frame height in pixels (e.g. 1080, 720, 1296) */
    uint32_t fps;               /**< Frames per second (e.g. 30, 60) */
    uint32_t bitrate_kbps;      /**< Target bitrate in Kbps (e.g. 4000) */
    uint8_t  codec;             /**< proto_codec_type_t */
    uint8_t  pixel_format;      /**< proto_pixel_format_t */
    uint8_t  gop_size;          /**< Group of Pictures (I-frame interval, e.g. 30) */
    uint8_t  rc_mode;           /**< 0: CBR, 1: VBR */
} proto_cam_config_t;

/**
 * @brief Payload for PROTO_RESP_CAM_GET_CONFIG
 */
typedef struct {
    int32_t            status;  /**< proto_status_t */
    proto_cam_config_t config;  /**< Current active configuration */
} proto_cam_get_config_resp_t;

/**
 * @brief Payload for PROTO_CMD_CAM_TAKE_PHOTO
 */
typedef struct {
    uint8_t  quality;           /**< JPEG compression quality (1 - 100) */
    uint8_t  burst_count;       /**< 1 for single shot, >1 for burst */
    uint16_t burst_interval_ms; /**< Delay between burst captures (ms) */
    char     prefix[16];        /**< Optional file prefix, e.g. "UAV_IMG" */
} proto_cam_take_photo_req_t;

/**
 * @brief Payload for PROTO_EVENT_CAM_PHOTO_TAKEN
 */
typedef struct {
    int32_t  status;            /**< PROTO_OK or write error */
    uint32_t file_size_bytes;   /**< Size of saved image */
    uint64_t timestamp_us;      /**< Capture timestamp (microsecond) */
    char     file_path[64];     /**< Full path on storage, e.g. "/mnt/sd/IMG_0001.jpg" */
} proto_cam_photo_taken_event_t;

/**
 * @brief Payload for PROTO_CMD_CAM_START_RECORD
 */
typedef struct {
    uint32_t max_duration_sec;  /**< Max segment length (0 = continuous) */
    uint32_t max_size_mb;       /**< Max file size before rotating (0 = unlimited) */
    char     dest_dir[48];      /**< Directory to store video, e.g. "/mnt/sd/video" */
} proto_cam_start_record_req_t;

/**
 * @brief Payload for PROTO_EVENT_CAM_RECORD_STATUS
 */
typedef struct {
    uint8_t  is_recording;      /**< 1: Currently recording, 0: Stopped */
    uint32_t elapsed_sec;       /**< Seconds recorded in current file */
    uint32_t current_size_kb;   /**< Current file size in Kilobytes */
    char     active_file[64];   /**< Active recording file name */
} proto_cam_record_status_event_t;

/**
 * @brief Payload for PROTO_CMD_CAM_SET_ISP_PARAMS & PROTO_RESP_CAM_GET_ISP_PARAMS
 */
typedef struct {
    uint8_t  exposure_mode;     /**< proto_exposure_mode_t */
    uint32_t exposure_time_us;  /**< Manual exposure time in microseconds */
    uint16_t gain;              /**< ISO / Sensor analog gain (100 - 6400) */
    uint8_t  wb_mode;           /**< proto_wb_mode_t */
    uint16_t color_temp_k;      /**< Manual Kelvin white balance (2000 - 10000K) */
    uint8_t  flip_horizontal;   /**< 1: Mirror horizontally */
    uint8_t  flip_vertical;     /**< 1: Flip vertically */
} proto_cam_isp_params_t;

/**
 * @brief Payload for PROTO_RESP_CAM_GET_ISP_PARAMS
 */
typedef struct {
    int32_t                status;  /**< proto_status_t */
    proto_cam_isp_params_t params;  /**< Current ISP parameters */
} proto_cam_get_isp_resp_t;

/**
 * @brief Payload for PROTO_RESP_CAM_GET_STATUS
 */
typedef struct {
    int32_t  status;            /**< proto_status_t */
    uint8_t  is_capturing;      /**< 1: Sensor streaming active */
    uint8_t  is_recording;      /**< 1: Recording to storage active */
    uint32_t captured_frames;   /**< Total frames captured since start */
    uint32_t dropped_frames;    /**< Total dropped frames */
    float    measured_fps;      /**< Real-time calculated FPS */
    int16_t  sensor_temp_c;     /**< Sensor temperature in Celsius */
} proto_cam_status_resp_t;

#pragma pack(pop)

/* ========================================================================== */
/*                              Helper Utilities                              */
/* ========================================================================== */

/**
 * @brief Convert codec enum to readable string
 */
const char* proto_codec_str(proto_codec_type_t codec);

/**
 * @brief Convert pixel format enum to readable string
 */
const char* proto_pix_fmt_str(proto_pixel_format_t fmt);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_CAMERA_H */
