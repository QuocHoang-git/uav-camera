#ifndef __UAV_CAMERA_DEVICE_HPP
#define __UAV_CAMERA_DEVICE_HPP

#include <cstdint>
#include <string>
#include <chrono>

#include "camera/types.hpp"
#include "hal/hal_camera.h"
#include "protocol/proto_camera.h"

namespace uav::camera {

/**
 * @brief RAII C++ wrapper for Rockchip V4L2 Camera HAL (hal_camera_t)
 *
 * Acquires video frames, manages DMA-BUF physical memory ring buffers,
 * and tracks real-time sensor metrics (FPS, dropped frames, total frames).
 */
class CameraDevice {
public:
    CameraDevice();
    ~CameraDevice();

    /* Non-copyable, movable */
    CameraDevice(const CameraDevice&) = delete;
    CameraDevice& operator=(const CameraDevice&) = delete;
    CameraDevice(CameraDevice&& other) noexcept;
    CameraDevice& operator=(CameraDevice&& other) noexcept;

    /**
     * @brief Open and initialize the camera hardware
     */
    bool init(const CameraServiceConfig& config);

    /**
     * @brief Close and release camera device resources
     */
    void close();

    /**
     * @brief Start video capture streaming on sensor
     */
    bool start();

    /**
     * @brief Stop video capture streaming
     */
    bool stop();

    /**
     * @brief Check whether sensor is currently streaming
     */
    bool isStreaming() const noexcept { return is_streaming_; }

    /**
     * @brief Fetch next available frame from sensor
     *
     * @param[out] out_frame Populated video frame struct (including dma_fd)
     * @param[in]  timeout_ms Timeout in milliseconds (0 for non-blocking)
     * @return true if frame was successfully dequeued
     */
    bool getFrame(hal_video_frame_t& out_frame, int timeout_ms = 0);

    /**
     * @brief Recycle / return buffer slot back to V4L2 queue
     *
     * @param buf_index Ring buffer index (0 .. buffer_count - 1)
     * @return true if buffer was successfully re-queued
     */
    bool releaseFrame(uint32_t buf_index);

    /**
     * @brief Retrieve underlying V4L2 device file descriptor for epoll multiplexing
     */
    int getFd() const;

    /**
     * @brief Retrieve active configuration
     */
    const CameraServiceConfig& getConfig() const noexcept { return config_; }

    /**
     * @brief Populate protocol status report
     */
    proto_cam_status_resp_t getStatus() const;

private:
    hal_camera_t*       hal_cam_      = nullptr;
    CameraServiceConfig config_{};
    bool                is_streaming_ = false;

    /* Metrics */
    uint32_t total_captured_frames_ = 0;
    uint32_t total_dropped_frames_  = 0;
    float    measured_fps_          = 0.0f;

    std::chrono::steady_clock::time_point last_fps_calc_time_;
    uint32_t fps_frame_counter_ = 0;

    void updateFpsCalculation();
};

} // namespace uav::camera

#endif // __UAV_CAMERA_DEVICE_HPP
