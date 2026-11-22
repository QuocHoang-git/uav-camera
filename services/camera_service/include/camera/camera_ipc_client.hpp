#ifndef __UAV_CAMERA_IPC_CLIENT_HPP
#define __UAV_CAMERA_IPC_CLIENT_HPP

#include <string>
#include <string_view>
#include <functional>
#include <atomic>

#include "camera/types.hpp"
#include "protocol/protocol.h"
#include "eipc/eipc.h"

namespace uav::camera {

/**
 * @brief EIPC Client wrapper for Camera Service
 *
 * Manages communication with manager_service, handles protocol packet decoding,
 * and transfers zero-copy DMA-BUF file descriptors to consumers.
 */
class CameraIpcClient {
public:
    using PacketHandler = std::function<void(const proto_packet_t& packet)>;
    using ConnectionHandler = std::function<void(bool is_connected)>;

    CameraIpcClient();
    ~CameraIpcClient();

    /* Non-copyable, movable */
    CameraIpcClient(const CameraIpcClient&) = delete;
    CameraIpcClient& operator=(const CameraIpcClient&) = delete;
    CameraIpcClient(CameraIpcClient&& other) noexcept;
    CameraIpcClient& operator=(CameraIpcClient&& other) noexcept;

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
     * @brief Register Camera Service identity with manager_service
     */
    bool registerWithManager();

    /* --- High-level Protocol Send Helpers --- */

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
     * @brief Publish a new video frame with its DMA-BUF file descriptor
     *
     * @param dst      Target service ID (e.g. PROTO_SVC_STREAMING or PROTO_SVC_BROADCAST)
     * @param meta     Video frame metadata
     * @param dma_fd   DMA-BUF file descriptor
     * @return proto_status_t
     */
    proto_status_t sendVideoFrame(proto_service_id_t dst,
                                  const proto_video_frame_meta_t& meta,
                                  int dma_fd);

    /* --- Callback Registrations --- */

    void setPacketHandler(PacketHandler handler) {
        on_packet_ = std::move(handler);
    }

    void setConnectionHandler(ConnectionHandler handler) {
        on_connection_ = std::move(handler);
    }

private:
    std::string     socket_path_;
    eipc_context_t* eipc_ctx_     = nullptr;
    int             server_peer_id_ = -1;
    bool            is_connected_ = false;

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

} // namespace uav::camera

#endif // __UAV_CAMERA_IPC_CLIENT_HPP
