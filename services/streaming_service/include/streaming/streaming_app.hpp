#ifndef __UAV_STREAMING_APP_HPP
#define __UAV_STREAMING_APP_HPP

#include <string>
#include <atomic>
#include <memory>
#include <chrono>

#include "streaming/types.hpp"
#include "streaming/rtsp_server.hpp"
#include "streaming/streaming_ipc_client.hpp"
#include "protocol/protocol.h"

namespace uav::streaming {

/**
 * @brief Master application engine for the UAV RTSP/WebRTC Streaming Service
 *
 * Implements a non-blocking Reactor event loop multiplexing:
 * 1. Linux signalfd (SIGINT, SIGTERM)
 * 2. EIPC client socket (control commands and incoming encoded frames from encoder_service)
 * 3. Network RTSP streaming lifecycle and instant IDR triggering
 */
class StreamingApp {
public:
    explicit StreamingApp(StreamingServiceConfig config = {});
    ~StreamingApp();

    /* Non-copyable, non-movable */
    StreamingApp(const StreamingApp&) = delete;
    StreamingApp& operator=(const StreamingApp&) = delete;
    StreamingApp(StreamingApp&&) = delete;
    StreamingApp& operator=(StreamingApp&&) = delete;

    /**
     * @brief Initialize all subsystem descriptors (signalfd, RtspServer, EIPC client, epoll)
     */
    bool init();

    /**
     * @brief Run the blocking event loop until stop() is called
     * @return Exit code (0 for success)
     */
    int run();

    /**
     * @brief Stop RTSP server and exit event loop
     */
    void stop();

    RtspServer* getRtspServer() noexcept { return rtsp_server_.get(); }
    StreamingIpcClient& getIpcClient() noexcept { return ipc_client_; }

private:
    StreamingServiceConfig config_;

    int epoll_fd_  = -1;
    int signal_fd_ = -1;

    std::atomic<bool> is_running_{false};
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point last_stats_time_;
    uint64_t last_bytes_sent_{0};

    std::unique_ptr<RtspServer> rtsp_server_;
    StreamingIpcClient          ipc_client_;

    /* Epoll initialization helpers */
    bool setupSignalfd();
    bool setupEpoll();

    /* Event handlers */
    void handleSignalEvent();
    void handleIncomingIpcPacket(const proto_packet_t& packet);
    void checkAndSendStats();

    /* Protocol command dispatchers */
    void dispatchEncodedFrame(const proto_packet_t& pkt);
    void dispatchStreamStart(const proto_packet_t& pkt);
    void dispatchStreamStop(const proto_packet_t& pkt);
    void dispatchGetConfig(const proto_packet_t& pkt);
    void dispatchSetConfig(const proto_packet_t& pkt);
    void dispatchGetStatus(const proto_packet_t& pkt);
    void dispatchPing(const proto_packet_t& pkt);
};

} // namespace uav::streaming

#endif // __UAV_STREAMING_APP_HPP
