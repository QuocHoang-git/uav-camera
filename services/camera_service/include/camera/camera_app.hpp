#ifndef __UAV_CAMERA_APP_HPP
#define __UAV_CAMERA_APP_HPP

#include <string>
#include <atomic>
#include <memory>

#include "camera/types.hpp"
#include "camera/camera_device.hpp"
#include "camera/camera_ipc_client.hpp"
#include "protocol/protocol.h"

namespace uav::camera {

/**
 * @brief Master application engine for the Camera Service
 *
 * Implements a non-blocking Reactor event loop multiplexing:
 * 1. Linux signalfd (SIGINT, SIGTERM)
 * 2. EIPC client socket (control commands from manager_service)
 * 3. V4L2 device file descriptor (incoming hardware video frames)
 */
class CameraApp {
public:
    explicit CameraApp(CameraServiceConfig config = {});
    ~CameraApp();

    /* Non-copyable, non-movable */
    CameraApp(const CameraApp&) = delete;
    CameraApp& operator=(const CameraApp&) = delete;
    CameraApp(CameraApp&&) = delete;
    CameraApp& operator=(CameraApp&&) = delete;

    /**
     * @brief Initialize all subsystem descriptors (signalfd, camera HAL, EIPC client, epoll)
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

    CameraDevice& getDevice() noexcept { return camera_dev_; }
    CameraIpcClient& getIpcClient() noexcept { return ipc_client_; }

private:
    CameraServiceConfig config_;

    int epoll_fd_  = -1;
    int signal_fd_ = -1;

    std::atomic<bool> is_running_{false};

    CameraDevice    camera_dev_;
    CameraIpcClient ipc_client_;

    /* Epoll initialization helpers */
    bool setupSignalfd();
    bool setupEpoll();
    void updateCameraFdInEpoll(bool enable);

    /* Event handlers */
    void handleSignalEvent();
    void handleCameraFrameReady();
    void handleIncomingIpcPacket(const proto_packet_t& packet);

    /* Protocol command dispatchers */
    void dispatchStartCapture(const proto_packet_t& pkt);
    void dispatchStopCapture(const proto_packet_t& pkt);
    void dispatchGetConfig(const proto_packet_t& pkt);
    void dispatchSetConfig(const proto_packet_t& pkt);
    void dispatchGetStatus(const proto_packet_t& pkt);
    void dispatchFrameRelease(const proto_packet_t& pkt);
    void dispatchPing(const proto_packet_t& pkt);
};

} // namespace uav::camera

#endif // __UAV_CAMERA_APP_HPP
