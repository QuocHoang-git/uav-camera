#ifndef __UAV_MANAGER_BROKER_HPP
#define __UAV_MANAGER_BROKER_HPP

#include <string>
#include <string_view>
#include <functional>
#include <array>
#include <optional>

#include "manager/types.hpp"
#include "protocol/protocol.h"
#include "eipc/eipc.h"

namespace uav::manager {

/**
 * @brief EIPC Broker manager and message router
 *
 * Encapsulates the lower-level eipc broker socket, coordinates client connections,
 * decodes application packets, and transparently routes inter-service messages.
 */
class BrokerManager {
public:
    using PacketHandler = std::function<void(int peer_id, const proto_packet_t& packet)>;
    using PeerEventHandler = std::function<void(int peer_id)>;

    BrokerManager();
    ~BrokerManager();

    /* Non-copyable, movable */
    BrokerManager(const BrokerManager&) = delete;
    BrokerManager& operator=(const BrokerManager&) = delete;
    BrokerManager(BrokerManager&&) noexcept;
    BrokerManager& operator=(BrokerManager&&) noexcept;

    /**
     * @brief Initialize EIPC broker and bind Unix Domain Socket
     */
    bool init(std::string_view socket_path = DEFAULT_SOCKET_PATH);

    /**
     * @brief Deinitialize and close broker socket
     */
    void deinit();

    /**
     * @brief Retrieve EIPC epoll file descriptor for external event loop multiplexing
     */
    int getEpollFd() const;

    /**
     * @brief Process queued I/O events on EIPC broker
     */
    void processEvents(uint32_t timeout_ms = 0);

    /**
     * @brief Forward a raw EIPC message to a specific peer
     */
    bool forwardMessage(int dest_peer_id, const eipc_message_t* msg);

    /**
     * @brief Broadcast a raw EIPC message to all connected peers except sender
     */
    void broadcastMessage(const eipc_message_t* msg, int exclude_peer_id = -1);

    /**
     * @brief Establish a zero-copy direct socketpair link between two client peers
     */
    bool createDirectLink(int peer_id1, int peer_id2);

    /* --- High-level protocol senders --- */

    proto_status_t sendCmd(int peer_id,
                           proto_service_id_t dst,
                           uint16_t msg_id,
                           uint16_t trans_id,
                           const void* payload = nullptr,
                           size_t payload_len = 0);

    proto_status_t sendResp(int peer_id,
                            proto_service_id_t dst,
                            uint16_t msg_id,
                            uint16_t trans_id,
                            const void* payload = nullptr,
                            size_t payload_len = 0);

    proto_status_t sendRespStatus(int peer_id,
                                  proto_service_id_t dst,
                                  uint16_t msg_id,
                                  uint16_t trans_id,
                                  proto_status_t status);

    proto_status_t sendEvent(int peer_id,
                             proto_service_id_t dst,
                             uint16_t msg_id,
                             const void* payload = nullptr,
                             size_t payload_len = 0);

    proto_status_t broadcastEvent(uint16_t msg_id,
                                  const void* payload = nullptr,
                                  size_t payload_len = 0);

    /* --- Callback registrations --- */

    void setPacketHandler(PacketHandler handler) {
        on_packet_ = std::move(handler);
    }

    void setPeerConnectHandler(PeerEventHandler handler) {
        on_peer_connect_ = std::move(handler);
    }

    void setPeerDisconnectHandler(PeerEventHandler handler) {
        on_peer_disconnect_ = std::move(handler);
    }

    /**
     * @brief Map a connected peer ID to its authenticated service identity
     */
    void bindPeerService(int peer_id, proto_service_id_t svc_id, std::string_view name);

    /**
     * @brief Find peer ID registered for a specific service ID
     */
    int findPeerIdByService(proto_service_id_t svc_id) const;

private:
    std::string     socket_path_;
    eipc_context_t* eipc_ctx_ = nullptr;

    std::array<PeerConnection, MAX_CLIENT_PEERS> peers_{};

    PacketHandler    on_packet_;
    PeerEventHandler on_peer_connect_;
    PeerEventHandler on_peer_disconnect_;

    /* C-style trampolines for EIPC C API */
    static void onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata);
    static void onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata);
    static void onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata);

    void handleConnect(int peer_id);
    void handleDisconnect(int peer_id);
    void handleMsg(int peer_id, const eipc_message_t* msg);

    PeerConnection* findPeer(int peer_id);
    const PeerConnection* findPeer(int peer_id) const;
};

} // namespace uav::manager

#endif // __UAV_MANAGER_BROKER_HPP
