#include "encoder/encoder_device.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace uav::encoder {

EncoderDevice::EncoderDevice() = default;

EncoderDevice::~EncoderDevice() {
    close();
}

EncoderDevice::EncoderDevice(EncoderDevice&& other) noexcept
    : hal_enc_(other.hal_enc_),
      config_(other.config_),
      is_encoding_(other.is_encoding_),
      total_encoded_frames_(other.total_encoded_frames_),
      total_dropped_frames_(other.total_dropped_frames_),
      measured_fps_(other.measured_fps_),
      measured_bitrate_kbps_(other.measured_bitrate_kbps_),
      last_metric_calc_time_(other.last_metric_calc_time_),
      metric_frame_counter_(other.metric_frame_counter_),
      metric_bytes_counter_(other.metric_bytes_counter_) {
    other.hal_enc_ = nullptr;
    other.is_encoding_ = false;
}

EncoderDevice& EncoderDevice::operator=(EncoderDevice&& other) noexcept {
    if (this != &other) {
        close();
        hal_enc_ = other.hal_enc_;
        config_ = other.config_;
        is_encoding_ = other.is_encoding_;
        total_encoded_frames_ = other.total_encoded_frames_;
        total_dropped_frames_ = other.total_dropped_frames_;
        measured_fps_ = other.measured_fps_;
        measured_bitrate_kbps_ = other.measured_bitrate_kbps_;
        last_metric_calc_time_ = other.last_metric_calc_time_;
        metric_frame_counter_ = other.metric_frame_counter_;
        metric_bytes_counter_ = other.metric_bytes_counter_;

        other.hal_enc_ = nullptr;
        other.is_encoding_ = false;
    }
    return *this;
}

bool EncoderDevice::init(const EncoderServiceConfig& config) {
    close();
    config_ = config;

    hal_encoder_config_t hal_cfg{};
    hal_cfg.codec        = config.codec;
    hal_cfg.width        = config.width;
    hal_cfg.height       = config.height;
    hal_cfg.fps          = config.fps;
    hal_cfg.bitrate_kbps = config.bitrate_kbps;
    hal_cfg.gop_size     = config.gop_size;
    hal_cfg.input_format = config.input_format;
    hal_cfg.rc_mode      = config.rc_mode;
    hal_cfg.profile      = config.profile;

    hal_enc_ = hal_encoder_create(&hal_cfg);
    if (!hal_enc_) {
        std::fprintf(stderr, "[EncoderDevice] Failed to create HAL encoder context\n");
        return false;
    }

    last_metric_calc_time_ = std::chrono::steady_clock::now();
    metric_frame_counter_  = 0;
    metric_bytes_counter_  = 0;

    std::printf("[EncoderDevice] Initialized %s encoder (%ux%u @ %ufps, %u Kbps)\n",
                (config.codec == HAL_CODEC_H265 ? "H.265" : "H.264"),
                config.width, config.height, config.fps, config.bitrate_kbps);
    return true;
}

void EncoderDevice::close() {
    if (hal_enc_) {
        if (is_encoding_) {
            stop();
        }
        hal_encoder_destroy(hal_enc_);
        hal_enc_ = nullptr;
    }
    is_encoding_ = false;
}

bool EncoderDevice::start() {
    if (!hal_enc_) return false;
    if (is_encoding_) return true;

    if (hal_encoder_start(hal_enc_) != 0) {
        std::fprintf(stderr, "[EncoderDevice] hal_encoder_start() failed\n");
        return false;
    }

    is_encoding_ = true;
    last_metric_calc_time_ = std::chrono::steady_clock::now();
    metric_frame_counter_ = 0;
    metric_bytes_counter_ = 0;
    return true;
}

bool EncoderDevice::stop() {
    if (!hal_enc_ || !is_encoding_) return true;

    if (hal_encoder_stop(hal_enc_) != 0) {
        std::fprintf(stderr, "[EncoderDevice] hal_encoder_stop() failed\n");
        return false;
    }

    is_encoding_ = false;
    return true;
}

bool EncoderDevice::sendFrame(const hal_video_frame_t& frame, int timeout_ms) {
    if (!hal_enc_ || !is_encoding_) return false;

    int ret = hal_encoder_send_frame(hal_enc_, &frame, timeout_ms);
    if (ret != 0) {
        total_dropped_frames_++;
        return false;
    }
    return true;
}

bool EncoderDevice::getPacket(hal_encoded_packet_t& out_packet, int timeout_ms) {
    if (!hal_enc_ || !is_encoding_) return false;

    int ret = hal_encoder_get_packet(hal_enc_, &out_packet, timeout_ms);
    if (ret != 0) {
        return false;
    }

    total_encoded_frames_++;
    updateMetrics(out_packet.size);
    return true;
}

bool EncoderDevice::releasePacket(hal_encoded_packet_t& packet) {
    if (!hal_enc_) return false;
    return (hal_encoder_release_packet(hal_enc_, &packet) == 0);
}

bool EncoderDevice::releasePacketIndex(uint32_t buf_index) {
    if (!hal_enc_) return false;
    return (hal_encoder_release_packet_index(hal_enc_, buf_index) == 0);
}

bool EncoderDevice::setBitrate(uint32_t bitrate_kbps) {
    if (!hal_enc_) return false;
    if (hal_encoder_set_bitrate(hal_enc_, bitrate_kbps) == 0) {
        config_.bitrate_kbps = bitrate_kbps;
        return true;
    }
    return false;
}

bool EncoderDevice::setFps(uint32_t fps) {
    if (!hal_enc_) return false;
    if (hal_encoder_set_fps(hal_enc_, fps) == 0) {
        config_.fps = fps;
        return true;
    }
    return false;
}

bool EncoderDevice::requestIdr() {
    if (!hal_enc_) return false;
    return (hal_encoder_request_idr(hal_enc_) == 0);
}

int EncoderDevice::getFd() const {
    return hal_enc_ ? hal_encoder_get_fd(hal_enc_) : -1;
}

proto_enc_status_resp_t EncoderDevice::getStatus() const {
    proto_enc_status_resp_t st{};
    st.status               = static_cast<int32_t>(PROTO_OK);
    st.is_encoding          = is_encoding_ ? 1 : 0;
    st.encoded_frames       = total_encoded_frames_;
    st.dropped_frames       = total_dropped_frames_;
    st.output_fps           = measured_fps_;
    st.current_bitrate_kbps = measured_bitrate_kbps_;
    st.vpu_load_pct         = is_encoding_ ? 35 : 0; /* Estimated VPU load on RV1106 */
    return st;
}

void EncoderDevice::updateMetrics(size_t packet_bytes) {
    metric_frame_counter_++;
    metric_bytes_counter_ += packet_bytes;

    if (total_encoded_frames_ == 1) {
        std::printf("[EncoderDevice] First packet encoded! (size=%zu bytes)\n", packet_bytes);
        std::fflush(stdout);
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_metric_calc_time_).count();

    if (elapsed_ms >= 2000) {
        measured_fps_ = static_cast<float>(metric_frame_counter_ * 1000.0f) / static_cast<float>(elapsed_ms);
        measured_bitrate_kbps_ = static_cast<uint32_t>((metric_bytes_counter_ * 8ULL) / elapsed_ms);

        metric_frame_counter_  = 0;
        metric_bytes_counter_  = 0;
        last_metric_calc_time_ = now;
        std::printf("[EncoderDevice] Encode: %.1f FPS, %u Kbps (total: %u packets, dropped: %u)\n",
                    measured_fps_, measured_bitrate_kbps_, total_encoded_frames_, total_dropped_frames_);
        std::fflush(stdout);
    }
}

} // namespace uav::encoder
