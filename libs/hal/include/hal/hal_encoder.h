#ifndef __HAL_ENCODER_H
#define __HAL_ENCODER_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hardware Video Encoder Configuration
 */
typedef struct {
    hal_codec_type_t    codec;          /**< HAL_CODEC_H264, HAL_CODEC_H265, HAL_CODEC_JPEG */
    uint32_t            width;          /**< Video frame width in pixels */
    uint32_t            height;         /**< Video frame height in pixels */
    uint32_t            fps;            /**< Target frame rate (e.g. 30, 60) */
    uint32_t            bitrate_kbps;   /**< Target bitrate in Kbps (e.g. 4000) */
    uint32_t            gop_size;       /**< Group of Pictures (I-frame interval, e.g. 30) */
    hal_pixel_format_t  input_format;   /**< Uncompressed pixel format (e.g. HAL_PIX_FMT_NV12) */
    int                 rc_mode;        /**< 0: CBR (Constant Bitrate), 1: VBR (Variable Bitrate) */
    int                 profile;        /**< 0: Baseline, 1: Main, 2: High */
} hal_encoder_config_t;

typedef struct hal_encoder hal_encoder_t;

/**
 * @brief Create and initialize hardware VPU encoder context (Rockchip MPP)
 *
 * @param config Encoder parameters
 * @return Pointer to opaque encoder handle, or NULL on failure
 */
hal_encoder_t* hal_encoder_create(const hal_encoder_config_t *config);

/**
 * @brief Release and destroy encoder context
 */
void           hal_encoder_destroy(hal_encoder_t *enc);

/**
 * @brief Start the encoder hardware pipeline
 */
int            hal_encoder_start(hal_encoder_t *enc);

/**
 * @brief Stop the encoder pipeline
 */
int            hal_encoder_stop(hal_encoder_t *enc);

/**
 * @brief Feed a raw uncompressed frame into the hardware encoder
 *
 * Supports DMA-BUF zero-copy ingestion via frame->dma_fd.
 *
 * @param enc        Encoder handle
 * @param frame      Input video frame metadata & buffer/fd
 * @param timeout_ms Timeout in milliseconds (-1 for blocking, 0 for non-blocking)
 * @return 0 on success, negative error code on failure
 */
int            hal_encoder_send_frame(hal_encoder_t *enc, const hal_video_frame_t *frame, int timeout_ms);

/**
 * @brief Retrieve a compressed video packet from the hardware encoder
 *
 * The output packet contains encoded H.264/H.265 NAL data or DMA-BUF descriptor.
 *
 * @param enc        Encoder handle
 * @param packet     Output packet struct populated with data/dma_fd and metadata
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative error code on failure/timeout
 */
int            hal_encoder_get_packet(hal_encoder_t *enc, hal_encoded_packet_t *packet, int timeout_ms);

/**
 * @brief Release / recycle an encoded packet buffer back to the encoder pool
 *
 * Must be called after consumer has processed the packet.
 */
int            hal_encoder_release_packet(hal_encoder_t *enc, hal_encoded_packet_t *packet);

/**
 * @brief Release / recycle an encoded packet buffer by slot index
 *
 * @param enc       Encoder handle
 * @param buf_index Slot index to release (0..HAL_ENCODER_MAX_PACKET_QUEUE-1)
 * @return 0 on success, negative error code on failure
 */
int            hal_encoder_release_packet_index(hal_encoder_t *enc, uint32_t buf_index);

/**
 * @brief Dynamically adjust output bitrate on-the-fly without restarting pipeline
 *
 * Essential for UAV radio-link telemetry rate adaptation.
 *
 * @param enc          Encoder handle
 * @param bitrate_kbps New target bitrate in Kbps
 * @return 0 on success, negative on failure
 */
int            hal_encoder_set_bitrate(hal_encoder_t *enc, uint32_t bitrate_kbps);

/**
 * @brief Dynamically adjust frame rate on-the-fly
 */
int            hal_encoder_set_fps(hal_encoder_t *enc, uint32_t fps);

/**
 * @brief Force the hardware encoder to generate an immediate IDR Keyframe
 *
 * Called when a new viewer joins the RTSP stream to allow instant playback.
 */
int            hal_encoder_request_idr(hal_encoder_t *enc);

/**
 * @brief Retrieve pollable file descriptor for epoll event loop integration
 *
 * Becomes readable (POLLIN) when an encoded packet is ready in the hardware queue.
 *
 * @param enc Encoder handle
 * @return File descriptor >= 0, or -1 if polling is not supported by driver
 */
int            hal_encoder_get_fd(const hal_encoder_t *enc);

#ifdef __cplusplus
}
#endif

#endif /* __HAL_ENCODER_H */
