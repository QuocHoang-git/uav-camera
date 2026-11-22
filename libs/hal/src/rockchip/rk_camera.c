#define _GNU_SOURCE

#include "hal/hal_camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <linux/videodev2.h>

#define HAL_CAMERA_MAX_BUFFERS     8
#define HAL_CAMERA_DEFAULT_BUFFERS 4
#define HAL_CAMERA_PATH_MAX 64

typedef struct {
    void   *vir_addr;
    size_t  length;
    int     dma_fd;
} hal_cam_buffer_t;

struct hal_camera {
    int                 v4l2_fd;
    enum v4l2_buf_type  buf_type;
    char                device_path[HAL_CAMERA_PATH_MAX];
    hal_camera_config_t config;
    bool                is_streaming;

    uint32_t            stride_w;
    uint32_t            stride_h;

    hal_cam_buffer_t    buffers[HAL_CAMERA_MAX_BUFFERS];
    uint32_t            allocated_buffers;
};

static bool hal_camera_query_capabilities(hal_camera_t *cam){
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(cam->v4l2_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("[HAL Camera] VIDIOC_QUERYCAP failed");
        return false;
    }

    uint32_t capabilities = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;

    if (capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        cam->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if (capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        cam->buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        fprintf(stderr, "[HAL Camera] Device does not support video capture!\n");
        return false;
    }

    if (!(capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "[HAL Camera] Device does not support streaming I/O!\n");
        return false;
    }
    return true;
}

static bool hal_camera_setup_format(hal_camera_t *cam){
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = cam->buf_type;

    if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        fmt.fmt.pix.width        = cam->config.width;
        fmt.fmt.pix.height       = cam->config.height;
        fmt.fmt.pix.pixelformat  = cam->config.format;
        fmt.fmt.pix.field        = V4L2_FIELD_NONE;
    } else {
        fmt.fmt.pix_mp.width       = cam->config.width;
        fmt.fmt.pix_mp.height      = cam->config.height;
        fmt.fmt.pix_mp.pixelformat = cam->config.format;
        fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes  = 1;
    }

    if (ioctl(cam->v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("[HAL Camera] VIDIOC_S_FMT failed");
        return false;
    }

    if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        cam->config.width  = fmt.fmt.pix.width;
        cam->config.height = fmt.fmt.pix.height;
        cam->stride_w      = fmt.fmt.pix.bytesperline;
        cam->stride_h      = fmt.fmt.pix.height;
    } else {
        cam->config.width  = fmt.fmt.pix_mp.width;
        cam->config.height = fmt.fmt.pix_mp.height;
        cam->stride_w      = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
        cam->stride_h      = fmt.fmt.pix_mp.height;
    }

    return true;
}

hal_camera_t* hal_camera_create(const hal_camera_config_t *config) {
    if (!config || !config->device_path) {
        fprintf(stderr, "[HAL Camera] Invalid config or device path is NULL\n");
        return NULL;
    }

    hal_camera_t *cam = (hal_camera_t *)calloc(1, sizeof(hal_camera_t));
    if (!cam) {
        perror("[HAL Camera] Failed to allocate memory");
        return NULL;
    }

    cam->config = *config;
    strncpy(cam->device_path, config->device_path, sizeof(cam->device_path) - 1);
    cam->device_path[sizeof(cam->device_path) - 1] = '\0';
    cam->config.device_path = cam->device_path;

    if (cam->config.buffer_count <= 0) {
        cam->config.buffer_count = HAL_CAMERA_DEFAULT_BUFFERS;
    } else if (cam->config.buffer_count > HAL_CAMERA_MAX_BUFFERS) {
        cam->config.buffer_count = HAL_CAMERA_MAX_BUFFERS;
    }

    cam->v4l2_fd = open(config->device_path, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
    if (cam->v4l2_fd < 0) {
        perror("[HAL Camera] open() device failed");
        free(cam);
        return NULL;
    }

    if(!hal_camera_query_capabilities(cam) || !hal_camera_setup_format(cam)){
        close(cam->v4l2_fd);
        free(cam);
        return NULL;
    }

    if (config->fps > 0) {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = cam->buf_type;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = config->fps;
        ioctl(cam->v4l2_fd, VIDIOC_S_PARM, &parm);
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = cam->config.buffer_count;
    req.type   = cam->buf_type;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cam->v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("[HAL Camera] VIDIOC_REQBUFS failed");
        close(cam->v4l2_fd);
        free(cam);
        return NULL;
    }

    if (req.count < 2) {
        fprintf(stderr, "[HAL Camera] Insufficient buffer memory on device!\n");
        close(cam->v4l2_fd);
        free(cam);
        return NULL;
    }

    cam->allocated_buffers = req.count;

    for (uint32_t i = 0; i < cam->allocated_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = cam->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        struct v4l2_plane planes[1];
        if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            memset(planes, 0, sizeof(planes));
            buf.m.planes = planes;
            buf.length = 1;
        }

        if (ioctl(cam->v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("[HAL Camera] VIDIOC_QUERYBUF failed");
            hal_camera_destroy(cam);
            return NULL;
        }

        size_t  length;
        off_t   offset;
        if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            length = buf.m.planes[0].length;
            offset = buf.m.planes[0].m.mem_offset;
        } else {
            length = buf.length;
            offset = buf.m.offset;
        }

        cam->buffers[i].length = length;
        cam->buffers[i].vir_addr = mmap(NULL, length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, cam->v4l2_fd, offset);
        if (cam->buffers[i].vir_addr == MAP_FAILED) {
            perror("[HAL Camera] mmap failed");
            cam->buffers[i].vir_addr = NULL;
            hal_camera_destroy(cam);
            return NULL;
        }

        // Export DMA-BUF FD
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type  = cam->buf_type;
        expbuf.index = i;
        expbuf.flags = O_CLOEXEC | O_RDWR;

        if (ioctl(cam->v4l2_fd, VIDIOC_EXPBUF, &expbuf) == 0) {
            cam->buffers[i].dma_fd = expbuf.fd;
        } else {
            cam->buffers[i].dma_fd = -1;
        }
    }

    printf("[HAL Camera] Created %s: %ux%u @ %ufps, %u buffers (DMA-BUF %s)\n",
           config->device_path, cam->config.width, cam->config.height, cam->config.fps,
           cam->allocated_buffers, (cam->buffers[0].dma_fd >= 0) ? "Enabled" : "Disabled");

    return cam;
}

int hal_camera_start(hal_camera_t *cam) {
    if (!cam || cam->v4l2_fd < 0) {
        return -1;
    }
    if (cam->is_streaming) {
        return 0;
    }

    for (uint32_t i = 0; i < cam->allocated_buffers; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = cam->buf_type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        struct v4l2_plane planes[1];
        if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            memset(planes, 0, sizeof(planes));
            buf.m.planes = planes;
            buf.length = 1;
        }

        if (ioctl(cam->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("[HAL Camera] Initial VIDIOC_QBUF failed");
            return -1;
        }
    }

    enum v4l2_buf_type type = cam->buf_type;
    if (ioctl(cam->v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("[HAL Camera] VIDIOC_STREAMON failed");
        return -1;
    }

    cam->is_streaming = true;
    printf("[HAL Camera] Streaming started\n");
    return 0;
}

int hal_camera_stop(hal_camera_t *cam) {
    if (!cam || cam->v4l2_fd < 0 || !cam->is_streaming) {
        return 0;
    }

    enum v4l2_buf_type type = cam->buf_type;
    if (ioctl(cam->v4l2_fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("[HAL Camera] VIDIOC_STREAMOFF failed");
        return -1;
    }

    cam->is_streaming = false;
    printf("[HAL Camera] Streaming stopped\n");
    return 0;
}

int hal_camera_get_frame(hal_camera_t *cam, hal_video_frame_t *frame, int timeout_ms) {
    if (!cam || !frame || cam->v4l2_fd < 0 || !cam->is_streaming) {
        return -EINVAL;
    }

    if (timeout_ms >= 0) {
        struct pollfd pfd;
        pfd.fd = cam->v4l2_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR) return -EINTR;
            perror("[HAL Camera] poll() error");
            return -errno;
        }
        if (ret == 0) {
            return -ETIMEDOUT;
        }
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = cam->buf_type;
    buf.memory = V4L2_MEMORY_MMAP;

    struct v4l2_plane planes[1];
    if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        memset(planes, 0, sizeof(planes));
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (ioctl(cam->v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -EAGAIN;
        }
        perror("[HAL Camera] VIDIOC_DQBUF failed");
        return -errno;
    }

    uint32_t idx = buf.index;
    if (idx >= cam->allocated_buffers) {
        return -EFAULT;
    }

    frame->buf_index    = idx;
    frame->vir_addr     = (uint8_t *)cam->buffers[idx].vir_addr;
    frame->dma_fd       = cam->buffers[idx].dma_fd;
    frame->width        = cam->config.width;
    frame->height       = cam->config.height;
    frame->stride_w     = cam->stride_w;
    frame->stride_h     = cam->stride_h;
    frame->format       = cam->config.format;
    frame->sequence     = buf.sequence;

    if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        frame->size = buf.m.planes[0].bytesused;
    } else {
        frame->size = buf.bytesused;
    }

    frame->timestamp_us = (uint64_t)buf.timestamp.tv_sec * 1000000ULL + buf.timestamp.tv_usec;

    return 0;
}

int hal_camera_release_frame(hal_camera_t *cam, hal_video_frame_t *frame) {
    if (!cam || !frame || cam->v4l2_fd < 0 || !cam->is_streaming) {
        return -EINVAL;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = cam->buf_type;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index  = frame->buf_index;

    struct v4l2_plane planes[1];
    if (cam->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        memset(planes, 0, sizeof(planes));
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (ioctl(cam->v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        perror("[HAL Camera] VIDIOC_QBUF release failed");
        return -errno;
    }

    return 0;
}

int hal_camera_get_fd(const hal_camera_t *cam) {
    return cam ? cam->v4l2_fd : -1;
}

void hal_camera_destroy(hal_camera_t *cam) {
    if (!cam) return;

    if (cam->is_streaming) {
        hal_camera_stop(cam);
    }
 
    for (uint32_t i = 0; i < cam->allocated_buffers; ++i) {
        if (cam->buffers[i].vir_addr && cam->buffers[i].length > 0) {
            munmap(cam->buffers[i].vir_addr, cam->buffers[i].length);
            cam->buffers[i].vir_addr = NULL;
        }
        if (cam->buffers[i].dma_fd >= 0) {
            close(cam->buffers[i].dma_fd);
            cam->buffers[i].dma_fd = -1;
        }
    }

    if (cam->v4l2_fd >= 0) {
        close(cam->v4l2_fd);
        cam->v4l2_fd = -1;
    }

    free(cam);
}
