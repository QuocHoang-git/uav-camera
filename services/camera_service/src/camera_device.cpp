#include "camera/camera_device.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

namespace uav::camera {

CameraDevice::CameraDevice() = default;

CameraDevice::~CameraDevice() {
    close();
}

CameraDevice::CameraDevice(CameraDevice&& other) noexcept
    : hal_cam_(other.hal_cam_),
      config_(std::move(other.config_)),
      is_streaming_(other.is_streaming_),
      total_captured_frames_(other.total_captured_frames_),
      total_dropped_frames_(other.total_dropped_frames_),
      measured_fps_(other.measured_fps_),
      last_fps_calc_time_(other.last_fps_calc_time_),
      fps_frame_counter_(other.fps_frame_counter_) {
    other.hal_cam_ = nullptr;
    other.is_streaming_ = false;
}

CameraDevice& CameraDevice::operator=(CameraDevice&& other) noexcept {
    if (this != &other) {
        close();
        hal_cam_ = other.hal_cam_;
        config_ = std::move(other.config_);
        is_streaming_ = other.is_streaming_;
        total_captured_frames_ = other.total_captured_frames_;
        total_dropped_frames_ = other.total_dropped_frames_;
        measured_fps_ = other.measured_fps_;
        last_fps_calc_time_ = other.last_fps_calc_time_;
        fps_frame_counter_ = other.fps_frame_counter_;

        other.hal_cam_ = nullptr;
        other.is_streaming_ = false;
    }
    return *this;
}

bool CameraDevice::init(const CameraServiceConfig& config) {
    close();
    config_ = config;

    hal_camera_config_t hal_cfg{};
    hal_cfg.device_path  = config_.device_path.c_str();
    hal_cfg.width        = config_.width;
    hal_cfg.height       = config_.height;
    hal_cfg.fps          = config_.fps;
    hal_cfg.format       = config_.pixel_format;
    hal_cfg.buffer_count = static_cast<int>(config_.buffer_count);

    hal_cam_ = hal_camera_create(&hal_cfg);
    if (!hal_cam_) {
        std::fprintf(stderr, "[CameraDevice] Failed to initialize HAL camera: %s\n", config_.device_path.c_str());
        return false;
    }

    std::printf("[CameraDevice] HAL Camera initialized: %s (%ux%u @ %ufps, %u buffers)\n",
                config_.device_path.c_str(), config_.width, config_.height, config_.fps, config_.buffer_count);
    return true;
}

void CameraDevice::close() {
    if (hal_cam_) {
        if (is_streaming_) {
            stop();
        }
        hal_camera_destroy(hal_cam_);
        hal_cam_ = nullptr;
    }
}

bool CameraDevice::start() {
    if (!hal_cam_) {
        return false;
    }
    if (is_streaming_) {
        return true;
    }

    if (hal_camera_start(hal_cam_) != 0) {
        std::fprintf(stderr, "[CameraDevice] hal_camera_start() failed\n");
        return false;
    }

    is_streaming_ = true;
    total_captured_frames_ = 0;
    total_dropped_frames_  = 0;
    fps_frame_counter_     = 0;
    measured_fps_          = static_cast<float>(config_.fps);
    last_fps_calc_time_    = std::chrono::steady_clock::now();

    std::printf("[CameraDevice] Video capture stream started on %s\n", config_.device_path.c_str());
    return true;
}

bool CameraDevice::stop() {
    if (!hal_cam_ || !is_streaming_) {
        return true;
    }

    hal_camera_stop(hal_cam_);
    is_streaming_ = false;
    std::printf("[CameraDevice] Video capture stream stopped on %s\n", config_.device_path.c_str());
    return true;
}

bool CameraDevice::getFrame(hal_video_frame_t& out_frame, int timeout_ms) {
    if (!hal_cam_ || !is_streaming_) {
        return false;
    }

    int ret = hal_camera_get_frame(hal_cam_, &out_frame, timeout_ms);
    if (ret != 0) {
        total_dropped_frames_++;
        return false;
    }

    total_captured_frames_++;
    fps_frame_counter_++;
    if (total_captured_frames_ == 1) {
        std::printf("[CameraDevice] First frame captured! (%ux%u, dma_fd=%d, size=%u bytes)\n",
                    out_frame.width, out_frame.height, out_frame.dma_fd, out_frame.size);
        std::fflush(stdout);
    }
    updateFpsCalculation();
    return true;
}

bool CameraDevice::releaseFrame(uint32_t buf_index) {
    if (!hal_cam_) {
        return false;
    }

    hal_video_frame_t frame{};
    frame.buf_index = buf_index;
    return (hal_camera_release_frame(hal_cam_, &frame) == 0);
}

int CameraDevice::getFd() const {
    return hal_cam_ ? hal_camera_get_fd(hal_cam_) : -1;
}

proto_cam_status_resp_t CameraDevice::getStatus() const {
    proto_cam_status_resp_t resp{};
    resp.status          = static_cast<int32_t>(PROTO_OK);
    resp.is_capturing    = is_streaming_ ? 1 : 0;
    resp.is_recording    = 0;
    resp.captured_frames = total_captured_frames_;
    resp.dropped_frames  = total_dropped_frames_;
    resp.measured_fps    = measured_fps_;
    resp.sensor_temp_c   = 45; /* Nominal sensor temperature */
    return resp;
}

void CameraDevice::updateFpsCalculation() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_fps_calc_time_).count();

    if (elapsed_ms >= 2000) {
        measured_fps_ = (static_cast<float>(fps_frame_counter_) * 1000.0f) / static_cast<float>(elapsed_ms);
        fps_frame_counter_ = 0;
        last_fps_calc_time_ = now;
        std::printf("[CameraDevice] Capture: %.1f FPS (total: %u frames, dropped: %u)\n",
                    measured_fps_, total_captured_frames_, total_dropped_frames_);
        std::fflush(stdout);
    }
}

} // namespace uav::camera
