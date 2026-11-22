#ifndef __UAV_RTSP_SERVER_HPP
#define __UAV_RTSP_SERVER_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <unordered_map>
#include <netinet/in.h>

namespace uav::streaming {

enum class SessionState {
    INIT,
    READY,
    PLAYING,
    CLOSED
};

struct RtspClientSession {
    int socket_fd{-1};
    std::string session_id;
    std::string stream_path;
    std::string client_ip;
    uint16_t client_port{0};
    bool is_tcp_interleaved{true};
    uint8_t rtp_channel{0};
    uint8_t rtcp_channel{1};

    /* UDP transport parameters */
    struct sockaddr_in client_rtp_addr{};
    int server_rtp_fd{-1};
    uint16_t server_rtp_port{60000};
    uint16_t client_rtp_port{0};
    uint16_t client_rtcp_port{0};

    uint16_t rtp_seq{0};
    uint32_t rtp_timestamp{0};
    SessionState state{SessionState::INIT};
};

struct StreamChannel {
    std::string path;
    uint32_t width{1920};
    uint32_t height{1080};
    uint32_t fps{30};
    uint16_t rtp_seq{1000};
    uint32_t rtp_timestamp{0};
    std::vector<uint8_t> sps_pps_header;
};

using ClientStateCallback = std::function<void(uint32_t client_id, const std::string& ip, uint16_t port, bool connected)>;
using ForceIdrCallback    = std::function<void(const std::string& stream_path)>;

/**
 * @brief Zero-Dependency Embedded RTSP/RTP Streaming Server
 *
 * Implements RFC 2326 (RTSP) and RFC 6184 (RTP Payload Format for H.264 video).
 * Supports both RTP/AVP/TCP (Interleaved) and RTP/AVP (UDP Unicast).
 */
class RtspServer {
public:
    explicit RtspServer(uint16_t port = 8554);
    ~RtspServer();

    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;
    RtspServer(RtspServer&&) = delete;
    RtspServer& operator=(RtspServer&&) = delete;

    /**
     * @brief Register a live streaming endpoint (e.g. "/live/main")
     */
    void addStream(const std::string& path, uint32_t width, uint32_t height, uint32_t fps);

    /**
     * @brief Start RTSP listener thread and network event loop
     */
    bool start();

    /**
     * @brief Stop RTSP server and disconnect all connected clients
     */
    void stop();

    /**
     * @brief Push an encoded H.264 Annex-B frame to all active viewing clients
     * @param path The endpoint URI path (e.g. "/live/main")
     * @param h264_data Raw H.264 byte stream with NALUs
     * @param size Size in bytes
     * @param is_keyframe Indicates IDR keyframe
     */
    void pushFrame(const std::string& path, const uint8_t* h264_data, size_t size, bool is_keyframe);

    /**
     * @brief Register callback when a client connects or disconnects
     */
    void setClientStateCallback(ClientStateCallback cb) { client_state_cb_ = std::move(cb); }

    /**
     * @brief Register callback to request an immediate IDR frame when client starts PLAY
     */
    void setForceIdrCallback(ForceIdrCallback cb) { force_idr_cb_ = std::move(cb); }

    [[nodiscard]] bool isRunning() const noexcept { return is_running_; }
    uint32_t getActiveClientsCount() const;
    uint64_t getTotalBytesSent() const noexcept { return total_bytes_sent_; }

private:
    void listenerLoop();
    void handleClient(int client_fd, const std::string& client_ip, uint16_t client_port);
    void processRtspRequest(int client_fd, const std::string& request, const std::string& client_ip, uint16_t client_port);
    void sendRtpPacket(RtspClientSession& session, const uint8_t* rtp_pkt, size_t pkt_len);
    void packetizeH264AndSend(RtspClientSession& session, StreamChannel& channel,
                              const uint8_t* nal_ptr, size_t nal_len, bool is_last_nal);

    uint16_t port_;
    int server_socket_{-1};
    std::atomic<bool> is_running_{false};
    std::thread listener_thread_;

    mutable std::recursive_mutex channels_mutex_;
    std::unordered_map<std::string, StreamChannel> channels_;

    mutable std::recursive_mutex sessions_mutex_;
    std::unordered_map<int, RtspClientSession> sessions_;

    std::atomic<uint64_t> total_bytes_sent_{0};
    ClientStateCallback   client_state_cb_;
    ForceIdrCallback      force_idr_cb_;
};

} // namespace uav::streaming

#endif // __UAV_RTSP_SERVER_HPP
