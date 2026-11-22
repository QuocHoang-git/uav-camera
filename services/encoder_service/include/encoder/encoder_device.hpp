#ifndef __UAV_ENCODER_DEVICE_HPP
#define __UAV_ENCODER_DEVICE_HPP

#include <cstdint>
#include <string>
#include <chrono>

#include "encoder/types.hpp"
#include "hal/hal_encoder.h"
#include "protocol/proto_encoder.h"

namespace uav::encoder {

/**
 * @brief RAII C++ wrapper for Rockchip VPU Hardware Encoder HAL (hal_encoder_t)
 *
 * Encodes uncompressed NV12 DMA-BUF frames into H.264/H.265 compressed packets,
 * provides on-the-fly bitrate/fps adaptation, and tracks encoding metrics.
 */
class EncoderDevice {
public:
    EncoderDevice();
    ~EncoderDevice();

    /* Non-copyable, movable */
    EncoderDevice(const EncoderDevice&) = delete;
    EncoderDevice& operator=(const EncoderDevice&) = delete;
    EncoderDevice(EncoderDevice&& other) noexcept;
    EncoderDevice& operator=(EncoderDevice&& other) noexcept;

    /**
     * @brief Create and initialize the VPU hardware encoder context
     */
    bool init(const EncoderServiceConfig& config);

    /**
     * @brief Release and destroy encoder context
     */
    void close();

    /**
     * @brief Start hardware encoding pipeline
     */
    bool start();

    /**
     * @brief Stop encoding pipeline
     */
    bool stop();

    /**
     * @brief Check whether encoder is running
     */
    bool isEncoding() const noexcept { return is_encoding_; }

    /**
     * @brief Ingest a raw uncompressed frame into VPU (Zero-Copy via DMA-BUF FD)
     */
    bool sendFrame(const hal_video_frame_t& frame, int timeout_ms = 0);

    /**
     * @brief Retrieve encoded packet from VPU output queue
     */
    bool getPacket(hal_encoded_packet_t& out_packet, int timeout_ms = 0);

    /**
     * @brief Return packet buffer back to encoder pool
     */
    bool releasePacket(hal_encoded_packet_t& packet);

    /**
     * @brief Return packet buffer back to encoder pool by slot index
     */
    bool releasePacketIndex(uint32_t buf_index);

    /**
     * @brief Dynamically adjust output bitrate on-the-fly (UAV radio rate adaptation)
     */
    bool setBitrate(uint32_t bitrate_kbps);

    /**
     * @brief Dynamically adjust target frame rate on-the-fly
     */
    bool setFps(uint32_t fps);

    /**
     * @brief Request immediate IDR keyframe (e.g. when RTSP client connects)
     */
    bool requestIdr();

    /**
     * @brief Retrieve pollable file descriptor for epoll multiplexing
     */
    int getFd() const;

    /**
     * @brief Retrieve active configuration
     */
    const EncoderServiceConfig& getConfig() const noexcept { return config_; }

    /**
     * @brief Populate protocol status report
     */
    proto_enc_status_resp_t getStatus() const;

private:
    hal_encoder_t*       hal_enc_     = nullptr;
    EncoderServiceConfig config_{};
    bool                 is_encoding_ = false;

    /* Metrics */
    uint32_t total_encoded_frames_  = 0;
    uint32_t total_dropped_frames_  = 0;
    float    measured_fps_          = 0.0f;
    uint32_t measured_bitrate_kbps_ = 0;

    std::chrono::steady_clock::time_point last_metric_calc_time_;
    uint32_t metric_frame_counter_ = 0;
    size_t   metric_bytes_counter_ = 0;

    void updateMetrics(size_t packet_bytes);
};

} // namespace uav::encoder

#endif // __UAV_ENCODER_DEVICE_HPP
