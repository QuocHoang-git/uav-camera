#ifndef __HAL_CAMERA_H
#define __HAL_CAMERA_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char         *device_path;
    uint32_t            width;
    uint32_t            height;
    uint32_t            fps;
    hal_pixel_format_t  format;
    int                 buffer_count;
} hal_camera_config_t;

typedef struct hal_camera hal_camera_t;

hal_camera_t* hal_camera_create(const hal_camera_config_t *config);
void          hal_camera_destroy(hal_camera_t *cam);

int           hal_camera_start(hal_camera_t *cam);
int           hal_camera_stop(hal_camera_t *cam);

int           hal_camera_get_frame(hal_camera_t *cam, hal_video_frame_t *frame, int timeout_ms);
int           hal_camera_release_frame(hal_camera_t *cam, hal_video_frame_t *frame);

int           hal_camera_get_fd(const hal_camera_t *cam);

#ifdef __cplusplus
}
#endif

#endif
