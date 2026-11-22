#ifndef __HAL_TYPES_H
#define __HAL_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAKE_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a)) | \
    (((uint32_t)(uint8_t)(b)) << 8) | \
    (((uint32_t)(uint8_t)(c)) << 16) | \
    (((uint32_t)(uint8_t)(d)) << 24))

enum {
    HAL_PIX_FMT_NV12 = MAKE_FOURCC('N', 'V', '1', '2'),
    HAL_PIX_FMT_NV21 = MAKE_FOURCC('N', 'V', '2', '1'),

    HAL_PIX_FMT_YUV420 = MAKE_FOURCC('Y', 'U', '1', '2'),
    HAL_PIX_FMT_YUYV  = MAKE_FOURCC('Y', 'U', 'Y', 'V'),
    HAL_PIX_FMT_UYVY  = MAKE_FOURCC('U', 'Y', 'V', 'Y'),
    
    HAL_PIX_FMT_RGB24 = MAKE_FOURCC('R', 'G', 'B', '3'),
    HAL_PIX_FMT_BGR24 = MAKE_FOURCC('B', 'G', 'R', '3'),
    HAL_PIX_FMT_RGBA  = MAKE_FOURCC('R', 'G', 'B', 'A'),
    HAL_PIX_FMT_BGRA  = MAKE_FOURCC('B', 'G', 'R', 'A'),
};
typedef uint32_t hal_pixel_format_t;

typedef enum {
    HAL_CODEC_H264 = 0,
    HAL_CODEC_H265,
    HAL_CODEC_JPEG,
} hal_codec_type_t;

typedef struct {
    uint8_t            *vir_addr;
    int                 dma_fd;
    uint32_t            size;
    uint32_t            buf_index;

    uint32_t            width;
    uint32_t            height;
    uint32_t            stride_w;
    uint32_t            stride_h;

    hal_pixel_format_t  format;
    uint64_t            timestamp_us;
    uint32_t            sequence;
} hal_video_frame_t;

typedef struct {
    uint8_t            *data;
    int                 dma_fd;         /**< DMA-BUF file descriptor for zero-copy output (-1 if unused) */
    uint32_t            size;
    bool                is_keyframe;
    uint64_t            timestamp_us;
    uint32_t            sequence;
    uint32_t            buf_index;      /**< Ring buffer slot index (0..7) */
} hal_encoded_packet_t;

#ifdef __cplusplus
}
#endif

#endif

