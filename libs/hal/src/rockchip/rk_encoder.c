#define _GNU_SOURCE

#include "hal/hal_encoder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/eventfd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* ========================================================================== */
/*                Rockchip MPP Hardware VPU Detection                         */
/* ========================================================================== */

#if defined(HAVE_ROCKCHIP_MPP) || (defined(__has_include) && (__has_include(<rockchip/rk_mpi.h>) || __has_include(<rk_mpi.h>)))
#define ROCKCHIP_MPP_SUPPORTED 1
#if defined(__has_include) && __has_include(<rockchip/rk_mpi.h>)
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_mpi_cmd.h>
#include <rockchip/rk_mpi_mb_cmd.h>
#include <rockchip/rk_venc_cfg.h>
#include <rockchip/rk_venc_cmd.h>
#include <rockchip/rk_venc_rc.h>
#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_err.h>
#include <rockchip/rk_type.h>
#else
#include <rk_mpi.h>
#include <rk_mpi_cmd.h>
#include <rk_mpi_mb_cmd.h>
#include <rk_venc_cfg.h>
#include <rk_venc_cmd.h>
#include <rk_venc_rc.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <mpp_err.h>
#include <rk_type.h>
#endif
#else
#define ROCKCHIP_MPP_SUPPORTED 0
#endif

/* ========================================================================== */
/*                           Common Constants                                 */
/* ========================================================================== */

#define HAL_ENCODER_MAX_PACKET_QUEUE  8
#define HAL_ENCODER_DEFAULT_BUF_SIZE  (256 * 1024) /* 256 KB per packet slot */

static uint64_t hal_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000000ULL) + (uint64_t)tv.tv_usec;
}

static int alloc_packet_dma_buffer(size_t size, void **out_ptr) {
    int heap_fd = open("/dev/rk_dma_heap/rk-dma-heap-cma", O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) {
        heap_fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    }
    if (heap_fd >= 0) {
        struct {
            uint64_t len;
            uint32_t fd;
            uint32_t fd_flags;
            uint64_t heap_flags;
        } data;
        memset(&data, 0, sizeof(data));
        data.len = size;
        data.fd_flags = O_RDWR | O_CLOEXEC;
        if (ioctl(heap_fd, _IOWR('H', 0x0, data), &data) == 0) {
            close(heap_fd);
            void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, data.fd, 0);
            if (ptr != MAP_FAILED) {
                *out_ptr = ptr;
                return (int)data.fd;
            }
            close(data.fd);
        } else {
            close(heap_fd);
        }
    }
    return -1;
}

/* ========================================================================== */
/*                   Structure Definition (Hardware + Mock)                   */
/* ========================================================================== */

typedef struct {
    uint8_t  *data;
    uint32_t  capacity;
    uint32_t  size;
    bool      is_keyframe;
    uint64_t  timestamp_us;
    uint32_t  sequence;
    int       dma_fd;
    bool      in_use;
    bool      delivered;
} hal_enc_slot_t;

struct hal_encoder {
    hal_encoder_config_t config;
    bool                 is_running;
    bool                 force_next_idr;

    int                  event_fd;     /**< eventfd for epoll readiness */
    uint32_t             frame_count;

    hal_enc_slot_t       packet_pool[HAL_ENCODER_MAX_PACKET_QUEUE];

#if ROCKCHIP_MPP_SUPPORTED
    MppCtx               mpp_ctx;
    MppApi              *mpp_mpi;
    MppEncCfg            mpp_cfg;
    MppCodingType        coding_type;
    int                  valloc_fd;    /**< /dev/mpi/valloc device for packet retrieval */
#endif
};

/* ========================================================================== */
/*                Rockchip MPP Hardware VPU Implementation                    */
/* ========================================================================== */

#if ROCKCHIP_MPP_SUPPORTED

static int g_enc_chan_counter = 0;

static int rk_mpp_init_hardware(hal_encoder_t *enc) {
    MPP_RET ret = MPP_OK;

    /* 1. Open /dev/mpi/valloc device node for circular bitstream buffer retrieval */
    enc->valloc_fd = open("/dev/mpi/valloc", O_RDWR);
    if (enc->valloc_fd < 0) {
        fprintf(stderr, "[HAL Encoder MPP] Warning: Failed to open /dev/mpi/valloc: %s\n", strerror(errno));
    }

    /* 2. Map codec type */
    if (enc->config.codec == HAL_CODEC_H265) {
        enc->coding_type = MPP_VIDEO_CodingHEVC;
    } else if (enc->config.codec == HAL_CODEC_JPEG) {
        enc->coding_type = MPP_VIDEO_CodingMJPEG;
    } else {
        enc->coding_type = MPP_VIDEO_CodingAVC; /* Default: H.264 */
    }

    /* 3. Create MPP context and API interface */
    ret = mpp_create(&enc->mpp_ctx, &enc->mpp_mpi);
    if (ret != MPP_OK || !enc->mpp_ctx || !enc->mpp_mpi) {
        fprintf(stderr, "[HAL Encoder MPP] mpp_create failed: ret=%d\n", ret);
        if (enc->valloc_fd >= 0) { close(enc->valloc_fd); enc->valloc_fd = -1; }
        return -1;
    }

    /* 4. RV1106 VPU requires mpp_init_ext with channel id attribute */
    vcodec_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = MPP_CTX_ENC;
    attr.coding = enc->coding_type;
    attr.chan_id = g_enc_chan_counter++;

    ret = mpp_init_ext(enc->mpp_ctx, &attr);
    if (ret != MPP_OK) {
        fprintf(stderr, "[HAL Encoder MPP] mpp_init_ext failed: ret=%d\n", ret);
        mpp_destroy(enc->mpp_ctx);
        enc->mpp_ctx = NULL;
        if (enc->valloc_fd >= 0) { close(enc->valloc_fd); enc->valloc_fd = -1; }
        return -1;
    }

    /* 5. Initialize encoder configuration */
    ret = mpp_enc_cfg_init(&enc->mpp_cfg);
    if (ret != MPP_OK || !enc->mpp_cfg) {
        fprintf(stderr, "[HAL Encoder MPP] mpp_enc_cfg_init failed\n");
        return -1;
    }

    /* 6. Configure Pre-processing (Resolution, Stride, Pixel Format) */
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "prep:width", enc->config.width);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "prep:height", enc->config.height);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "prep:hor_stride", enc->config.width);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "prep:ver_stride", enc->config.height);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "prep:format", MPP_FMT_YUV420SP); /* NV12 */

    /* 7. Configure Rate Control: CBR for deterministic UAV telemetry/streaming */
    MppEncRcMode rc_mode = (enc->config.rc_mode == 0) ? MPP_ENC_RC_MODE_CBR : MPP_ENC_RC_MODE_VBR;
    int32_t bps = (int32_t)(enc->config.bitrate_kbps * 1000);

    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:mode", rc_mode);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_target", bps);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_max", (bps * 6) / 5);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_min", (bps * 4) / 5);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_in_flex", 0);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_in_num", enc->config.fps);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_in_denorm", 1);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_out_flex", 0);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_out_num", enc->config.fps);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_out_denorm", 1);
    mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:gop", enc->config.gop_size);

    /* 8. Codec-specific Profile & Level settings */
    if (enc->config.codec == HAL_CODEC_H264) {
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "codec:type", MPP_VIDEO_CodingAVC);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "h264:profile", 100); /* High Profile */
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "h264:level", 41);    /* Level 4.1 */
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "h264:cabac_en", 1);
    } else if (enc->config.codec == HAL_CODEC_H265) {
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "codec:type", MPP_VIDEO_CodingHEVC);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "h265:profile", 1);   /* Main Profile */
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "h265:level", 120);   /* Level 4.0 */
    }

    ret = enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_CFG, enc->mpp_cfg);
    if (ret != MPP_OK) {
        fprintf(stderr, "[HAL Encoder MPP] MPP_ENC_SET_CFG failed: ret=%d\n", ret);
        return -1;
    }

    /* 9. Prepend SPS/PPS headers to every IDR frame for instant RTSP/VLC sync */
    MppEncHeaderMode hdr_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
    enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_HEADER_MODE, &hdr_mode);

    fprintf(stdout, "[HAL Encoder MPP] Rockchip Hardware VPU initialized successfully (%s, chan=%d)\n",
            (enc->config.codec == HAL_CODEC_H265) ? "H.265" : "H.264", attr.chan_id);
    return 0;
}

static void rk_mpp_deinit_hardware(hal_encoder_t *enc) {
    if (!enc) return;

    if (enc->valloc_fd >= 0) {
        close(enc->valloc_fd);
        enc->valloc_fd = -1;
    }

    if (enc->mpp_cfg) {
        mpp_enc_cfg_deinit(enc->mpp_cfg);
        enc->mpp_cfg = NULL;
    }

    if (enc->mpp_ctx) {
        mpp_destroy(enc->mpp_ctx);
        enc->mpp_ctx = NULL;
    }

    enc->mpp_mpi = NULL;
}

#endif /* ROCKCHIP_MPP_SUPPORTED */

/* ========================================================================== */
/*                           Public HAL API Implementation                    */
/* ========================================================================== */

hal_encoder_t* hal_encoder_create(const hal_encoder_config_t *config) {
    if (!config || config->width == 0 || config->height == 0) {
        return NULL;
    }

    hal_encoder_t *enc = (hal_encoder_t *)calloc(1, sizeof(hal_encoder_t));
    if (!enc) {
        return NULL;
    }

    enc->config = *config;
    if (enc->config.gop_size == 0)   enc->config.gop_size = 30;
    if (enc->config.fps == 0)        enc->config.fps = 30;
    if (enc->config.bitrate_kbps == 0) enc->config.bitrate_kbps = 4000;

    enc->event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (enc->event_fd < 0) {
        free(enc);
        return NULL;
    }

    /* Initialize packet pool slots */
    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        enc->packet_pool[i].capacity     = HAL_ENCODER_DEFAULT_BUF_SIZE;
        void *dma_ptr = NULL;
        int dfd = alloc_packet_dma_buffer(enc->packet_pool[i].capacity, &dma_ptr);
        if (dfd >= 0) {
            enc->packet_pool[i].dma_fd   = dfd;
            enc->packet_pool[i].data     = (uint8_t *)dma_ptr;
        } else {
            enc->packet_pool[i].dma_fd   = -1;
            enc->packet_pool[i].data     = (uint8_t *)malloc(enc->packet_pool[i].capacity);
        }
        enc->packet_pool[i].size         = 0;
        enc->packet_pool[i].is_keyframe  = false;
        enc->packet_pool[i].timestamp_us = 0;
        enc->packet_pool[i].sequence     = 0;
        enc->packet_pool[i].in_use       = false;
    }

#if ROCKCHIP_MPP_SUPPORTED
    enc->valloc_fd = -1;
    if (rk_mpp_init_hardware(enc) != 0) {
        fprintf(stderr, "[HAL Encoder] Failed to initialize Rockchip MPP hardware VPU\n");
        hal_encoder_destroy(enc);
        return NULL;
    }
#else
    fprintf(stdout, "[HAL Encoder MOCK] Running in Software/Mock mode (Host PC, memfd DMA-BUF enabled)\n");
#endif

    fprintf(stdout, "[HAL Encoder] VPU Encoder Ready: %ux%u @ %ufps, %s, %u Kbps, GOP=%u\n",
            enc->config.width, enc->config.height, enc->config.fps,
            (enc->config.codec == HAL_CODEC_H265) ? "H.265" : "H.264",
            enc->config.bitrate_kbps, enc->config.gop_size);

    return enc;
}

void hal_encoder_destroy(hal_encoder_t *enc) {
    if (!enc) return;

    hal_encoder_stop(enc);

#if ROCKCHIP_MPP_SUPPORTED
    rk_mpp_deinit_hardware(enc);
#endif

    if (enc->event_fd >= 0) {
        close(enc->event_fd);
        enc->event_fd = -1;
    }

    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (enc->packet_pool[i].dma_fd >= 0) {
            if (enc->packet_pool[i].data) {
                munmap(enc->packet_pool[i].data, enc->packet_pool[i].capacity);
                enc->packet_pool[i].data = NULL;
            }
            close(enc->packet_pool[i].dma_fd);
            enc->packet_pool[i].dma_fd = -1;
        } else if (enc->packet_pool[i].data) {
            free(enc->packet_pool[i].data);
            enc->packet_pool[i].data = NULL;
        }
    }

    free(enc);
}

int hal_encoder_start(hal_encoder_t *enc) {
    if (!enc) return -1;
    if (enc->is_running) return 0;

    enc->is_running = true;
    enc->frame_count = 0;
    enc->force_next_idr = true; /* Always force IDR on first frame */

    fprintf(stdout, "[HAL Encoder] Hardware VPU encoder started\n");
    return 0;
}

int hal_encoder_stop(hal_encoder_t *enc) {
    if (!enc) return -1;
    if (!enc->is_running) return 0;

    enc->is_running = false;

    /* Flush pending packet queue */
    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        enc->packet_pool[i].in_use = false;
    }

    fprintf(stdout, "[HAL Encoder] Hardware VPU encoder stopped\n");
    return 0;
}

int hal_encoder_get_fd(const hal_encoder_t *enc) {
    return enc ? enc->event_fd : -1;
}

int hal_encoder_set_bitrate(hal_encoder_t *enc, uint32_t bitrate_kbps) {
    if (!enc || bitrate_kbps == 0) return -1;

    enc->config.bitrate_kbps = bitrate_kbps;

#if ROCKCHIP_MPP_SUPPORTED
    if (enc->mpp_ctx && enc->mpp_cfg && enc->mpp_mpi) {
        int32_t bps = (int32_t)(bitrate_kbps * 1000);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_target", bps);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_max", (bps * 6) / 5);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:bps_min", (bps * 4) / 5);
        enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_CFG, enc->mpp_cfg);
    }
#endif

    fprintf(stdout, "[HAL Encoder] Dynamic bitrate adjusted to: %u Kbps\n", bitrate_kbps);
    return 0;
}

int hal_encoder_set_fps(hal_encoder_t *enc, uint32_t fps) {
    if (!enc || fps == 0) return -1;

    enc->config.fps = fps;

#if ROCKCHIP_MPP_SUPPORTED
    if (enc->mpp_ctx && enc->mpp_cfg && enc->mpp_mpi) {
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_in_num", fps);
        mpp_enc_cfg_set_s32(enc->mpp_cfg, "rc:fps_out_num", fps);
        enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_CFG, enc->mpp_cfg);
    }
#endif

    fprintf(stdout, "[HAL Encoder] Dynamic framerate adjusted to: %u FPS\n", fps);
    return 0;
}

int hal_encoder_request_idr(hal_encoder_t *enc) {
    if (!enc) return -1;

    enc->force_next_idr = true;

#if ROCKCHIP_MPP_SUPPORTED
    if (enc->mpp_ctx && enc->mpp_mpi) {
        enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_IDR_FRAME, NULL);
    }
#endif

    fprintf(stdout, "[HAL Encoder] Instant IDR keyframe requested\n");
    return 0;
}

int hal_encoder_send_frame(hal_encoder_t *enc, const hal_video_frame_t *frame, int timeout_ms) {
    (void)timeout_ms;
    if (!enc || !enc->is_running || !frame) return -1;

#if ROCKCHIP_MPP_SUPPORTED
    if (enc->valloc_fd < 0) {
        return -1;
    }

    /* 1. Allocate a slot in packet pool for the encoded output with auto-reclaim failsafe */
    hal_enc_slot_t *slot = NULL;
    uint64_t now_us = hal_get_time_us();

    /* Failsafe: if a slot has been held for > 500ms (consumer crashed/lagged), auto-reclaim */
    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (enc->packet_pool[i].in_use && (now_us - enc->packet_pool[i].timestamp_us > 500000ULL)) {
            fprintf(stderr, "[HAL Encoder] Slot %d timed out (>500ms) without consumer release! Force reclaiming...\n", i);
            enc->packet_pool[i].in_use = false;
            enc->packet_pool[i].delivered = false;
        }
    }

    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (!enc->packet_pool[i].in_use) {
            slot = &enc->packet_pool[i];
            break;
        }
    }

    if (!slot) {
        /* Drop-on-Full: Network/consumer is congested, drop frame to protect bitstream integrity */
        enc->force_next_idr = true;
        fprintf(stderr, "[HAL Encoder] Packet pool full (consumer lagging)! Dropping frame %u, forcing next IDR\n", frame->sequence);
        return -EAGAIN;
    }

    /* 2. Prepare MPP Frame */
    MppFrame mpp_frame = NULL;
    MPP_RET ret = mpp_frame_init(&mpp_frame);
    if (ret != MPP_OK || !mpp_frame) {
        fprintf(stderr, "[HAL Encoder] mpp_frame_init failed: %d\n", ret);
        return -1;
    }

    mpp_frame_set_width(mpp_frame, enc->config.width);
    mpp_frame_set_height(mpp_frame, enc->config.height);
    mpp_frame_set_hor_stride(mpp_frame, frame->stride_w ? frame->stride_w : enc->config.width);
    mpp_frame_set_ver_stride(mpp_frame, frame->stride_h ? frame->stride_h : enc->config.height);
    mpp_frame_set_fmt(mpp_frame, MPP_FMT_YUV420SP);
    mpp_frame_set_pts(mpp_frame, frame->timestamp_us ? frame->timestamp_us : hal_get_time_us());

    /* 3. Prepare MPP Buffer (Zero-Copy DMA-BUF or Normal Memory) */
    MppBuffer mpp_buf = NULL;
    MppBufferInfo info;
    memset(&info, 0, sizeof(info));
    info.size = frame->size;
    info.fd = -1;

    if (frame->dma_fd >= 0) {
        info.type = MPP_BUFFER_TYPE_EXT_DMA;
        info.fd   = dup(frame->dma_fd);
        if (info.fd < 0) {
            fprintf(stderr, "[HAL Encoder] dup(dma_fd=%d) failed: %s\n", frame->dma_fd, strerror(errno));
            mpp_frame_deinit(&mpp_frame);
            return -1;
        }
    } else if (frame->vir_addr) {
        info.type = MPP_BUFFER_TYPE_NORMAL;
        info.ptr  = frame->vir_addr;
    }

    ret = mpp_buffer_import(&mpp_buf, &info);
    if (ret != MPP_OK || !mpp_buf) {
        fprintf(stderr, "[HAL Encoder] mpp_buffer_import failed: ret=%d, mpp_buf=%p (type=%d, fd=%d, size=%u)\n",
                ret, (void*)mpp_buf, info.type, info.fd, info.size);
        if (info.fd >= 0) close(info.fd);
        mpp_frame_deinit(&mpp_frame);
        return -1;
    }

    mpp_frame_set_buffer(mpp_frame, mpp_buf);

    /* Check if an IDR keyframe was requested */
    if (enc->force_next_idr) {
        enc->mpp_mpi->control(enc->mpp_ctx, MPP_ENC_SET_IDR_FRAME, NULL);
        enc->force_next_idr = false;
    }

    /* 4. Submit frame to hardware VPU */
    ret = enc->mpp_mpi->encode_put_frame(enc->mpp_ctx, mpp_frame);
    if (ret != MPP_OK) {
        fprintf(stderr, "[HAL Encoder] encode_put_frame failed: %d\n", ret);
        mpp_frame_deinit(&mpp_frame);
        mpp_buffer_put(mpp_buf);
        return -1;
    }

    /* 5. Dequeue encoded packet using struct venc_packet (exactly like smart-edge-camera) */
    struct venc_packet venc_pkt;
    memset(&venc_pkt, 0, sizeof(venc_pkt));
    struct venc_packet* p_pkt = &venc_pkt;

    int retry = 0;
    do {
        ret = enc->mpp_mpi->encode_get_packet(enc->mpp_ctx, (MppPacket*)&p_pkt);
        if (ret == MPP_OK && venc_pkt.len > 0) {
            break;
        }
        usleep(500); // 500us
    } while (++retry < 30); // Up to 15ms wait

    /* 6. CRUCIAL: Release input frame and imported DMA buffer AFTER encode_get_packet! */
    mpp_frame_deinit(&mpp_frame);
    mpp_buffer_put(mpp_buf);

    if (ret != MPP_OK || venc_pkt.len == 0) {
        fprintf(stderr, "[HAL Encoder] encode_get_packet failed: ret=%d, len=%u, retries=%d\n",
                ret, venc_pkt.len, retry);
        return -EAGAIN;
    }

    /* 7. Retrieve bitstream via /dev/mpi/valloc ioctl */
    struct valloc_mb mb;
    memset(&mb, 0, sizeof(mb));
    mb.mpi_buf_id = (int)venc_pkt.u64priv_data;
    mb.struct_size = sizeof(mb);

    int io_ret = ioctl(enc->valloc_fd, VALLOC_IOCTL_MB_GET_FD, &mb);
    if (io_ret < 0 || mb.dma_buf_fd < 0) {
        enc->mpp_mpi->encode_release_packet(enc->mpp_ctx, (MppPacket*)&p_pkt);
        return -EAGAIN;
    }

    void* mapped_ptr = mmap(NULL, venc_pkt.buf_size, PROT_READ, MAP_SHARED, mb.dma_buf_fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        close(mb.dma_buf_fd);
        enc->mpp_mpi->encode_release_packet(enc->mpp_ctx, (MppPacket*)&p_pkt);
        return -EAGAIN;
    }

    /* Ensure slot capacity */
    if (venc_pkt.len > slot->capacity) {
        uint32_t new_cap = venc_pkt.len + 32768;
        uint8_t *new_data = (uint8_t *)realloc(slot->data, new_cap);
        if (!new_data) {
            munmap(mapped_ptr, venc_pkt.buf_size);
            close(mb.dma_buf_fd);
            enc->mpp_mpi->encode_release_packet(enc->mpp_ctx, (MppPacket*)&p_pkt);
            return -EAGAIN;
        }
        slot->data = new_data;
        slot->capacity = new_cap;
    }

    /* Copy bitstream, handling circular ring buffer wrap-around */
    const uint8_t* base = (const uint8_t*)mapped_ptr;
    size_t offset = venc_pkt.offset;
    size_t len = venc_pkt.len;
    size_t buf_size = venc_pkt.buf_size;

    if (offset + len <= buf_size) {
        memcpy(slot->data, base + offset, len);
    } else {
        size_t part1 = buf_size - offset;
        size_t part2 = len - part1;
        memcpy(slot->data, base + offset, part1);
        memcpy(slot->data + part1, base, part2);
    }

    /* Unmap and close DMA-BUF FD, release packet back to VPU driver */
    munmap(mapped_ptr, venc_pkt.buf_size);
    close(mb.dma_buf_fd);
    enc->mpp_mpi->encode_release_packet(enc->mpp_ctx, (MppPacket*)&p_pkt);

    /* Determine IDR keyframe */
    bool is_idr = false;
    const uint8_t* p = slot->data;
    if (enc->config.codec == HAL_CODEC_H264) {
        for (size_t i = 0; i + 4 < len; ++i) {
            if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
                uint8_t nal_type = p[i+3] & 0x1F;
                if (nal_type == 5 || nal_type == 7) {
                    is_idr = true;
                    break;
                }
            } else if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x00 && p[i+3] == 0x01) {
                uint8_t nal_type = p[i+4] & 0x1F;
                if (nal_type == 5 || nal_type == 7) {
                    is_idr = true;
                    break;
                }
            }
        }
    } else if (enc->config.codec == HAL_CODEC_H265) {
        for (size_t i = 0; i + 4 < len; ++i) {
            if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x01) {
                uint8_t nal_type = (p[i+3] >> 1) & 0x3F;
                if (nal_type >= 16 && nal_type <= 21) {
                    is_idr = true;
                    break;
                }
            } else if (p[i] == 0x00 && p[i+1] == 0x00 && p[i+2] == 0x00 && p[i+3] == 0x01) {
                uint8_t nal_type = (p[i+4] >> 1) & 0x3F;
                if (nal_type >= 16 && nal_type <= 21) {
                    is_idr = true;
                    break;
                }
            }
        }
    }

    slot->in_use       = true;
    slot->delivered    = false;
    slot->size         = len;
    slot->is_keyframe  = is_idr;
    slot->timestamp_us = (uint64_t)venc_pkt.u64pts;
    slot->sequence     = enc->frame_count++;

    /* Signal epoll that packet is ready */
    eventfd_write(enc->event_fd, 1);
    return 0;

#else
    /* --- Software / Mock Simulation Path --- */
    hal_enc_slot_t *slot = NULL;
    uint64_t now_us = hal_get_time_us();

    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (enc->packet_pool[i].in_use && (now_us - enc->packet_pool[i].timestamp_us > 500000ULL)) {
            enc->packet_pool[i].in_use = false;
            enc->packet_pool[i].delivered = false;
        }
    }

    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (!enc->packet_pool[i].in_use) {
            slot = &enc->packet_pool[i];
            break;
        }
    }

    if (!slot) {
        enc->force_next_idr = true;
        fprintf(stderr, "[HAL Encoder] Packet pool full (mock), dropping frame %u, forcing IDR\n", frame->sequence);
        return -EAGAIN;
    }

    bool is_idr = enc->force_next_idr || ((enc->frame_count % enc->config.gop_size) == 0);
    enc->force_next_idr = false;

    slot->in_use = true;
    slot->delivered = false;
    slot->is_keyframe = is_idr;
    slot->sequence = enc->frame_count++;
    slot->timestamp_us = (frame->timestamp_us > 0) ? frame->timestamp_us : hal_get_time_us();

    uint8_t *p = slot->data;
    if (enc->config.codec == HAL_CODEC_H264) {
        p[0] = 0x00; p[1] = 0x00; p[2] = 0x00; p[3] = 0x01;
        p[4] = is_idr ? 0x65 : 0x41;
        slot->size = is_idr ? 32768 : 12288;
    } else {
        p[0] = 0x00; p[1] = 0x00; p[2] = 0x00; p[3] = 0x01;
        p[4] = is_idr ? 0x26 : 0x02;
        slot->size = is_idr ? 24576 : 8192;
    }

    if (slot->size > 5 && slot->size <= slot->capacity) {
        memset(p + 5, 0xAA, slot->size - 5);
    }

    eventfd_write(enc->event_fd, 1);
    return 0;
#endif
}

int hal_encoder_get_packet(hal_encoder_t *enc, hal_encoded_packet_t *packet, int timeout_ms) {
    (void)timeout_ms;
    if (!enc || !enc->is_running || !packet) {
        return -1;
    }

    /* Clear eventfd */
    eventfd_t val;
    eventfd_read(enc->event_fd, &val);

    /* Dequeue the first ready packet from pool that hasn't been delivered yet */
    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (enc->packet_pool[i].in_use && !enc->packet_pool[i].delivered) {
            enc->packet_pool[i].delivered = true;
            packet->data         = enc->packet_pool[i].data;
            packet->dma_fd       = (enc->packet_pool[i].dma_fd >= 0) ? dup(enc->packet_pool[i].dma_fd) : -1;
            packet->size         = enc->packet_pool[i].size;
            packet->is_keyframe  = enc->packet_pool[i].is_keyframe;
            packet->timestamp_us = enc->packet_pool[i].timestamp_us;
            packet->sequence     = enc->packet_pool[i].sequence;
            packet->buf_index    = (uint32_t)i;
            return 0;
        }
    }

    return -EAGAIN;
}

int hal_encoder_release_packet_index(hal_encoder_t *enc, uint32_t buf_index) {
    if (!enc || buf_index >= HAL_ENCODER_MAX_PACKET_QUEUE) {
        return -1;
    }
    enc->packet_pool[buf_index].in_use = false;
    enc->packet_pool[buf_index].delivered = false;
    return 0;
}

int hal_encoder_release_packet(hal_encoder_t *enc, hal_encoded_packet_t *packet) {
    if (!enc || !packet) {
        return -1;
    }

    if (packet->buf_index < HAL_ENCODER_MAX_PACKET_QUEUE) {
        return hal_encoder_release_packet_index(enc, packet->buf_index);
    }

    for (int i = 0; i < HAL_ENCODER_MAX_PACKET_QUEUE; ++i) {
        if (enc->packet_pool[i].in_use && enc->packet_pool[i].data == packet->data) {
            enc->packet_pool[i].in_use = false;
            enc->packet_pool[i].delivered = false;
            return 0;
        }
    }
    return -1;
}
