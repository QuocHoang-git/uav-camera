#include "hal/hal_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *device_path = "/dev/video0";
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t fps = 30;
    int target_frames = 30;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            device_path = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        }
    }

    printf("=== Testing HAL Camera (rk_camera) ===\n");
    printf("Device   : %s\n", device_path);
    printf("Config   : %ux%u @ %ufps, target: %d frames\n", width, height, fps, target_frames);

    hal_camera_config_t config = {
        .device_path = device_path,
        .width = width,
        .height = height,
        .fps = fps,
        .format = HAL_PIX_FMT_NV12,
        .buffer_count = 4
    };

    hal_camera_t *cam = hal_camera_create(&config);
    if (!cam) {
        fprintf(stderr, "\n[INFO] Could not open camera device '%s'.\n"
                        "On host development machines without a physical camera or v4l2loopback,\n"
                        "this error is expected. On the physical RV1106 target board, verify the MIPI sensor driver.\n",
                        device_path);
        return 0; /* Graceful exit on environments without hardware */
    }

    printf("[PASS] Camera device created successfully\n");

    int cam_fd = hal_camera_get_fd(cam);
    printf("[PASS] Camera V4L2 FD: %d\n", cam_fd);

    if (hal_camera_start(cam) != 0) {
        fprintf(stderr, "[FAIL] hal_camera_start() failed\n");
        hal_camera_destroy(cam);
        return 1;
    }
    printf("[PASS] hal_camera_start()\n");

    int captured = 0;
    for (int i = 0; i < target_frames; ++i) {
        hal_video_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        int ret = hal_camera_get_frame(cam, &frame, 1000);
        if (ret == 0) {
            captured++;
            printf("Captured frame #%u: size=%u, dma_fd=%d, buf_index=%u\n",
                   frame.sequence, frame.size, frame.dma_fd, frame.buf_index);
            hal_camera_release_frame(cam, &frame);
        } else {
            fprintf(stderr, "Frame timeout or drop on index %d\n", i);
        }
    }

    hal_camera_stop(cam);
    hal_camera_destroy(cam);

    printf("=== Captured %d / %d frames successfully ===\n", captured, target_frames);
    return 0;
}
