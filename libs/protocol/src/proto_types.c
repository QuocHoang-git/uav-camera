#include "protocol/proto_types.h"
#include "protocol/proto_system.h"
#include "protocol/proto_camera.h"
#include "protocol/proto_video_frame.h"
#include "protocol/proto_streaming.h"
#include "protocol/proto_telemetry.h"

/* ========================================================================== */
/*                           Status & Core Converters                         */
/* ========================================================================== */

const char* proto_status_str(proto_status_t status) {
    switch (status) {
        case PROTO_OK:                return "OK";
        case PROTO_ERR_GENERIC:       return "ERR_GENERIC";
        case PROTO_ERR_INVALID_ARG:   return "ERR_INVALID_ARG";
        case PROTO_ERR_NO_MEM:        return "ERR_NO_MEM";
        case PROTO_ERR_BUSY:          return "ERR_BUSY";
        case PROTO_ERR_TIMEOUT:       return "ERR_TIMEOUT";
        case PROTO_ERR_NOT_SUPPORTED: return "ERR_NOT_SUPPORTED";
        case PROTO_ERR_NOT_PERMITTED: return "ERR_NOT_PERMITTED";
        case PROTO_ERR_NOT_FOUND:     return "ERR_NOT_FOUND";
        case PROTO_ERR_IO:            return "ERR_IO";
        case PROTO_ERR_BAD_PAYLOAD:   return "ERR_BAD_PAYLOAD";
        case PROTO_ERR_SERVICE_DOWN:  return "ERR_SERVICE_DOWN";
        default:                      return "ERR_UNKNOWN";
    }
}

const char* proto_service_name(proto_service_id_t svc_id) {
    switch (svc_id) {
        case PROTO_SVC_BROKER:        return "Broker";
        case PROTO_SVC_MANAGER:       return "Manager";
        case PROTO_SVC_CAMERA:        return "Camera";
        case PROTO_SVC_STREAMING:     return "Streaming";
        case PROTO_SVC_TELEMETRY:     return "Telemetry";
        case PROTO_SVC_AI:            return "AI_Inference";
        case PROTO_SVC_STORAGE:       return "Storage";
        case PROTO_SVC_ENCODER:       return "Encoder";
        case PROTO_SVC_BROADCAST:     return "Broadcast";
        case PROTO_SVC_UNKNOWN:
        default:                      return "UnknownService";
    }
}

const char* proto_msg_type_str(proto_msg_type_t type) {
    switch (type) {
        case PROTO_MSG_TYPE_CMD:      return "CMD";
        case PROTO_MSG_TYPE_RESP:     return "RESP";
        case PROTO_MSG_TYPE_EVENT:    return "EVENT";
        case PROTO_MSG_TYPE_UNKNOWN:
        default:                      return "UNKNOWN_TYPE";
    }
}

/* ========================================================================== */
/*                           System State Converters                          */
/* ========================================================================== */

const char* proto_sys_state_str(proto_sys_state_t state) {
    switch (state) {
        case SYS_STATE_INIT:          return "INIT";
        case SYS_STATE_STANDBY:       return "STANDBY";
        case SYS_STATE_STREAMING:     return "STREAMING";
        case SYS_STATE_RECORDING:     return "RECORDING";
        case SYS_STATE_STREAM_RECORD: return "STREAM_AND_RECORD";
        case SYS_STATE_CALIBRATION:   return "CALIBRATION";
        case SYS_STATE_LOW_POWER:     return "LOW_POWER";
        case SYS_STATE_ERROR:         return "ERROR";
        case SYS_STATE_SHUTDOWN:      return "SHUTDOWN";
        case SYS_STATE_UNKNOWN:
        default:                      return "UNKNOWN";
    }
}

/* ========================================================================== */
/*                           Camera & Video Converters                        */
/* ========================================================================== */

const char* proto_codec_str(proto_codec_type_t codec) {
    switch (codec) {
        case PROTO_CODEC_H264:        return "H.264";
        case PROTO_CODEC_H265:        return "H.265";
        case PROTO_CODEC_JPEG:        return "JPEG";
        default:                      return "UNKNOWN_CODEC";
    }
}

const char* proto_pix_fmt_str(proto_pixel_format_t fmt) {
    switch (fmt) {
        case PROTO_PIX_FMT_NV12:      return "NV12";
        case PROTO_PIX_FMT_NV21:      return "NV21";
        case PROTO_PIX_FMT_YUYV:      return "YUYV";
        case PROTO_PIX_FMT_RGB24:     return "RGB24";
        case PROTO_PIX_FMT_BGR24:     return "BGR24";
        default:                      return "UNKNOWN_FMT";
    }
}

const char* proto_frame_type_str(proto_frame_type_t type) {
    switch (type) {
        case PROTO_FRAME_TYPE_RAW:          return "RAW_FRAME";
        case PROTO_FRAME_TYPE_ENCODED_IDR:  return "IDR_KEYFRAME";
        case PROTO_FRAME_TYPE_ENCODED_P:    return "P_FRAME";
        case PROTO_FRAME_TYPE_ENCODED_B:    return "B_FRAME";
        case PROTO_FRAME_TYPE_ENCODED_JPEG: return "JPEG_FRAME";
        default:                            return "UNKNOWN_FRAME";
    }
}

/* ========================================================================== */
/*                         Streaming & Network Converters                     */
/* ========================================================================== */

const char* proto_stream_proto_str(proto_stream_proto_t proto) {
    switch (proto) {
        case PROTO_STREAM_PROTO_RTSP_UDP: return "RTSP_UDP";
        case PROTO_STREAM_PROTO_RTSP_TCP: return "RTSP_TCP";
        case PROTO_STREAM_PROTO_WEBRTC:   return "WEBRTC";
        default:                          return "UNKNOWN_STREAM_PROTO";
    }
}

/* ========================================================================== */
/*                         Telemetry & Flight Converters                      */
/* ========================================================================== */

const char* proto_fc_link_state_str(proto_fc_link_state_t state) {
    switch (state) {
        case PROTO_FC_LINK_CONNECTED:    return "CONNECTED";
        case PROTO_FC_LINK_LOST:         return "LOST";
        case PROTO_FC_LINK_DISCONNECTED: return "DISCONNECTED";
        default:                         return "UNKNOWN";
    }
}
