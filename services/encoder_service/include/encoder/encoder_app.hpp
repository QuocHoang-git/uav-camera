#ifndef __UAV_ENCODER_APP_HPP
#define __UAV_ENCODER_APP_HPP

#include <string>
#include <atomic>
#include <memory>

#include "encoder/types.hpp"
#include "encoder/encoder_device.hpp"
#include "encoder/encoder_ipc_client.hpp"
#include "protocol/protocol.h"

namespace uav::encoder {

/**
 * @brief Master application engine for the Hardware VPU Encoder Service
 *
 * Implements a non-blocking Reactor event loop multiplexing:
 * 1. Linux signalfd (SIGINT, SIGTERM)
 * 2. EIPC client socket (control commands and incoming raw frames from camera_service)
 * 3. VPU eventfd (hardware encoded packet ready notifications)
 */
class EncoderApp {
public:
    explicit EncoderApp(EncoderServiceConfig config = {});
    ~EncoderApp();

    /* Non-copyable, non-movable */
    EncoderApp(const EncoderApp&) = delete;
    EncoderApp& operator=(const EncoderApp&) = delete;
    EncoderApp(EncoderApp&&) = delete;
    EncoderApp& operator=(EncoderApp&&) = delete;

    /**
     * @brief Initialize all subsystem descriptors (signalfd, VPU HAL, EIPC client, epoll)
     */
    bool init();

    /**
     * @brief Run the blocking event loop until stop() is called
     * @return Exit code (0 for success)
     */
    int run();

    /**
     * @brief Stop the event loop
     */
    void stop();

    EncoderDevice& getDevice() noexcept { return encoder_dev_; }
    EncoderIpcClient& getIpcClient() noexcept { return ipc_client_; }

private:
    EncoderServiceConfig config_;

    int epoll_fd_  = -1;
    int signal_fd_ = -1;

    std::atomic<bool> is_running_{false};

    EncoderDevice    encoder_dev_;
    EncoderIpcClient ipc_client_;

    /* Epoll initialization helpers */
    bool setupSignalfd();
    bool setupEpoll();
    void updateEncoderFdInEpoll(bool enable);

    /* Event handlers */
    void handleSignalEvent();
    void handleEncoderPacketReady();
    void handleIncomingIpcPacket(const proto_packet_t& packet);

    /* Protocol command dispatchers */
    void dispatchRawFrame(const proto_packet_t& pkt);
    void dispatchPacketRelease(const proto_packet_t& pkt);
    void dispatchStartEncoder(const proto_packet_t& pkt);
    void dispatchStopEncoder(const proto_packet_t& pkt);
    void dispatchGetConfig(const proto_packet_t& pkt);
    void dispatchSetConfig(const proto_packet_t& pkt);
    void dispatchSetBitrate(const proto_packet_t& pkt);
    void dispatchGetStatus(const proto_packet_t& pkt);
    void dispatchForceIdr(const proto_packet_t& pkt);
    void dispatchPing(const proto_packet_t& pkt);
};

} // namespace uav::encoder

#endif // __UAV_ENCODER_APP_HPP
