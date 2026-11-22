#ifndef __UAV_ENCODER_IPC_CLIENT_HPP
#define __UAV_ENCODER_IPC_CLIENT_HPP

#include <string>
#include <string_view>
#include <functional>
#include <atomic>

#include "encoder/types.hpp"
#include "protocol/protocol.h"
#include "eipc/eipc.h"

namespace uav::encoder {

/**
 * @brief EIPC Client wrapper for Encoder Service
 *
 * Manages communication with manager_service broker, decodes incoming
 * camera raw frames (with DMA-BUF), forwards compressed video frames to
 * streaming_service, and sends frame release commands back to camera_service.
 */
class EncoderIpcClient {
public:
    using PacketHandler = std::function<void(const proto_packet_t& packet)>;
    using ConnectionHandler = std::function<void(bool is_connected)>;

    EncoderIpcClient();
    ~EncoderIpcClient();

    /* Non-copyable, movable */
    EncoderIpcClient(const EncoderIpcClient&) = delete;
    EncoderIpcClient& operator=(const EncoderIpcClient&) = delete;
    EncoderIpcClient(EncoderIpcClient&& other) noexcept;
    EncoderIpcClient& operator=(EncoderIpcClient&& other) noexcept;

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
     * @brief Register Encoder Service identity with manager_service
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
     * @brief Publish an encoded video packet (H.264/H.265) with DMA-BUF file descriptor
     */
    proto_status_t sendEncodedFrame(proto_service_id_t dst,
                                    const proto_video_frame_meta_t& meta,
                                    int dma_fd);

    /**
     * @brief Return raw frame buffer back to camera_service so V4L2 queue can reuse it
     */
    proto_status_t sendFrameRelease(proto_service_id_t camera_svc_id,
                                    uint32_t buf_index,
                                    uint32_t sequence);

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

} // namespace uav::encoder

#endif // __UAV_ENCODER_IPC_CLIENT_HPP
