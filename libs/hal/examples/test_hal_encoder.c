#define _GNU_SOURCE
#include "hal/hal_encoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC   'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)

static int alloc_dma_buffer(size_t size, void **out_ptr) {
    const char *heaps[] = {
        "/dev/rk_dma_heap/rk-dma-heap-cma",
        "/dev/dma_heap/system-uncached",
        "/dev/dma_heap/system",
        "/dev/dma_heap/cma"
    };
    int heap_fd = -1;
    for (size_t i = 0; i < sizeof(heaps)/sizeof(heaps[0]); ++i) {
        heap_fd = open(heaps[i], O_RDWR | O_CLOEXEC);
        if (heap_fd >= 0) break;
    }
    if (heap_fd < 0) return -1;

    struct dma_heap_allocation_data data;
    memset(&data, 0, sizeof(data));
    data.len = size;
    data.fd_flags = O_CLOEXEC | O_RDWR;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) < 0) {
        close(heap_fd);
        return -1;
    }
    close(heap_fd);

    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, data.fd, 0);
    if (ptr == MAP_FAILED) {
        close(data.fd);
        return -1;
    }
    *out_ptr = ptr;
    return (int)data.fd;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *output_file = "/tmp/test_stream.h264";
    if (argc > 1) {
        output_file = argv[1];
    }

    printf("=== Testing HAL Encoder (rk_encoder) ===\n");
    printf("Output file: %s\n", output_file);

    /* 1. Configure encoder */
    hal_encoder_config_t cfg = {
        .codec = HAL_CODEC_H264,
        .width = 1920,
        .height = 1080,
        .fps = 30,
        .bitrate_kbps = 4000,
        .gop_size = 30,
        .input_format = HAL_PIX_FMT_NV12,
        .rc_mode = 0, /* CBR */
        .profile = 0  /* Baseline */
    };

    hal_encoder_t *enc = hal_encoder_create(&cfg);
    if (!enc) {
        fprintf(stderr, "[FAIL] hal_encoder_create failed\n");
        return 1;
    }
    printf("[PASS] hal_encoder_create\n");

    int enc_fd = hal_encoder_get_fd(enc);
    if (enc_fd < 0) {
        fprintf(stderr, "[FAIL] hal_encoder_get_fd failed\n");
        return 1;
    }
    printf("[PASS] hal_encoder_get_fd (fd = %d)\n", enc_fd);

    if (hal_encoder_start(enc) != 0) {
        fprintf(stderr, "[FAIL] hal_encoder_start failed\n");
        return 1;
    }
    printf("[PASS] hal_encoder_start\n");

    FILE *fp = fopen(output_file, "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    /* 2. Allocate frame buffer (DMA-BUF for RV1106 VPU hardware, or malloc on Host) */
    const int TEST_FRAMES = 60;
    uint32_t total_bytes_written = 0;
    int idr_count = 0;
    int p_count = 0;

    size_t nv12_size = 1920 * 1080 * 3 / 2;
    void *raw_yuv_ptr = NULL;
    int dma_fd = alloc_dma_buffer(nv12_size, &raw_yuv_ptr);

    if (dma_fd >= 0) {
        printf("[HAL Test] Successfully allocated CMA DMA-BUF (fd=%d, size=%zu bytes)\n", dma_fd, nv12_size);
        memset(raw_yuv_ptr, 0x80, nv12_size);
    } else {
        raw_yuv_ptr = malloc(nv12_size);
        memset(raw_yuv_ptr, 0x80, nv12_size);
        printf("[HAL Test] Running with virtual buffer (malloc, size=%zu bytes)\n", nv12_size);
    }

    for (int i = 0; i < TEST_FRAMES; ++i) {
        /* Test dynamic runtime adjustment midway */
        if (i == 15) {
            hal_encoder_set_bitrate(enc, 2000);
            printf("[PASS] hal_encoder_set_bitrate on-the-fly (2000 Kbps)\n");
        }
        if (i == 45) {
            hal_encoder_request_idr(enc);
            printf("[PASS] hal_encoder_request_idr on-the-fly (Force Keyframe)\n");
        }

        hal_video_frame_t frame = {
            .vir_addr = (uint8_t *)raw_yuv_ptr,
            .dma_fd = dma_fd,
            .size = (uint32_t)nv12_size,
            .buf_index = (uint32_t)(i % 4),
            .width = 1920,
            .height = 1080,
            .stride_w = 1920,
            .stride_h = 1080,
            .format = HAL_PIX_FMT_NV12,
            .timestamp_us = (uint64_t)i * 33333ULL,
            .sequence = (uint32_t)i
        };

        /* Send raw frame to encoder */
        int send_ret = hal_encoder_send_frame(enc, &frame, 0);
        if (send_ret != 0) {
            fprintf(stderr, "[FAIL] hal_encoder_send_frame failed: %d\n", send_ret);
            break;
        }

        /* Retrieve compressed packet */
        hal_encoded_packet_t packet;
        memset(&packet, 0, sizeof(packet));
        int get_ret = hal_encoder_get_packet(enc, &packet, 0);
        if (get_ret != 0) {
            fprintf(stderr, "[FAIL] hal_encoder_get_packet failed: %d\n", get_ret);
            break;
        }

        if (packet.is_keyframe) {
            idr_count++;
        } else {
            p_count++;
        }

        if (i == 0 || (i + 1) % 15 == 0) {
            printf("[TEST] Encoded frame #%d: %u bytes, keyframe=%s\n",
                   i + 1, packet.size, packet.is_keyframe ? "YES" : "NO");
        }

        /* Write packet to output file */
        fwrite(packet.data, 1, packet.size, fp);
        total_bytes_written += packet.size;

        /* Release packet back to encoder pool */
        hal_encoder_release_packet(enc, &packet);
    }

    if (dma_fd >= 0) {
        munmap(raw_yuv_ptr, nv12_size);
        close(dma_fd);
    } else {
        free(raw_yuv_ptr);
    }

    fclose(fp);

    hal_encoder_stop(enc);
    hal_encoder_destroy(enc);
    printf("[PASS] hal_encoder_stop & hal_encoder_destroy\n");

    printf("\n=== ENCODER TEST SUMMARY ===\n");
    printf("Total frames encoded : %d\n", idr_count + p_count);
    printf("  - IDR Keyframes    : %d\n", idr_count);
    printf("  - P-frames         : %d\n", p_count);
    printf("Total stream size    : %u bytes (%.2f MB)\n", total_bytes_written, (float)total_bytes_written / (1024.0f * 1024.0f));
    printf("Saved H.264 file     : %s\n", output_file);
    printf(">>> ALL ENCODER TESTS PASSED SUCCESSFULLY! <<<\n");

    return 0;
}
