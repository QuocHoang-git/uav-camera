#ifndef __UAV_STREAMING_IPC_CLIENT_HPP
#define __UAV_STREAMING_IPC_CLIENT_HPP

#include <string>
#include <string_view>
#include <functional>
#include <atomic>

#include "streaming/types.hpp"
#include "protocol/protocol.h"
#include "eipc/eipc.h"

namespace uav::streaming {

/**
 * @brief EIPC Client wrapper for Streaming Service
 *
 * Manages communication with manager_service broker, decodes incoming
 * encoded video packets (with DMA-BUF / memfd FD) from encoder_service,
 * sends force IDR requests, and reports streaming status & client events.
 */
class StreamingIpcClient {
public:
    using PacketHandler = std::function<void(const proto_packet_t& packet)>;
    using ConnectionHandler = std::function<void(bool is_connected)>;

    StreamingIpcClient();
    ~StreamingIpcClient();

    /* Non-copyable, movable */
    StreamingIpcClient(const StreamingIpcClient&) = delete;
    StreamingIpcClient& operator=(const StreamingIpcClient&) = delete;
    StreamingIpcClient(StreamingIpcClient&& other) noexcept;
    StreamingIpcClient& operator=(StreamingIpcClient&& other) noexcept;

    /**
     * @brief Connect to manager_service EIPC broker socket
     */
    bool init(std::string_view socket_path = DEFAULT_SOCKET_PATH);

    /**
     * @brief Disconnect and release EIPC context
     */
    void deinit();

    /**
     * @brief Check if connected to broker
     */
    bool isConnected() const noexcept { return is_connected_; }

    /**
     * @brief Retrieve EIPC epoll file descriptor for external event loop multiplexing
     */
    int getEpollFd() const;

    /**
     * @brief Process queued EIPC socket events
     */
    void processEvents(uint32_t timeout_ms = 0);

    /**
     * @brief Register Streaming Service identity with manager_service
     */
    bool registerWithManager();

    /* --- High-level Protocol Send Helpers --- */

    proto_status_t sendCommand(proto_service_id_t dst,
                               uint16_t msg_id,
                               uint16_t trans_id,
                               const void* payload = nullptr,
                               size_t payload_len = 0);

    proto_status_t sendResponse(proto_service_id_t dst,
                                uint16_t msg_id,
                                uint16_t trans_id,
                                const void* payload = nullptr,
                                size_t payload_len = 0);

    proto_status_t sendResponseStatus(proto_service_id_t dst,
                                      uint16_t msg_id,
                                      uint16_t trans_id,
                                      proto_status_t status);

    proto_status_t sendEvent(proto_service_id_t dst,
                             uint16_t msg_id,
                             const void* payload = nullptr,
                             size_t payload_len = 0);

    /**
     * @brief Request an immediate IDR / Keyframe from encoder_service
     */
    proto_status_t sendForceIdr(proto_service_id_t encoder_svc_id = PROTO_SVC_ENCODER,
                                uint8_t stream_id = 0);

    /**
     * @brief Release / recycle an encoded packet buffer back to encoder_service
     */
    proto_status_t sendFrameRelease(proto_service_id_t encoder_svc_id,
                                    uint32_t buf_index,
                                    uint32_t sequence);

    /**
     * @brief Report viewer client connected event to manager_service
     */
    proto_status_t sendClientConnectedEvent(uint32_t client_id,
                                           const std::string& ip,
                                           uint16_t port,
                                           uint8_t active_clients);

    /**
     * @brief Report viewer client disconnected event to manager_service
     */
    proto_status_t sendClientDisconnectedEvent(uint32_t client_id,
                                              const std::string& ip,
                                              uint16_t port,
                                              uint8_t active_clients);

    /**
     * @brief Report periodic streaming distribution stats to manager_service
     */
    proto_status_t sendStreamStatsEvent(uint8_t active_clients,
                                        uint32_t outgoing_kbps,
                                        float packet_loss_pct,
                                        uint64_t total_bytes);

    /* --- Callback Registrations --- */

    void setPacketHandler(PacketHandler handler) {
        on_packet_ = std::move(handler);
    }

    void setConnectionHandler(ConnectionHandler handler) {
        on_connection_ = std::move(handler);
    }

private:
    std::string     socket_path_;
    eipc_context_t* eipc_ctx_       = nullptr;
    int             server_peer_id_ = -1;
    bool            is_connected_   = false;
    uint16_t        next_trans_id_  = 1;

    PacketHandler     on_packet_;
    ConnectionHandler on_connection_;

    /* Static trampolines for C EIPC callbacks */
    static void onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata);
    static void onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata);
    static void onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata);

    void handleConnect(int peer_id);
    void handleDisconnect(int peer_id);
    void handleMsg(int peer_id, const eipc_message_t* msg);
};

} // namespace uav::streaming

#endif // __UAV_STREAMING_IPC_CLIENT_HPP
