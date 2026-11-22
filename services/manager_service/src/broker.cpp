#include "manager/broker.hpp"
#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace uav::manager {

BrokerManager::BrokerManager() = default;

BrokerManager::~BrokerManager() {
    deinit();
}

BrokerManager::BrokerManager(BrokerManager&& other) noexcept
    : socket_path_(std::move(other.socket_path_)),
      eipc_ctx_(other.eipc_ctx_),
      peers_(std::move(other.peers_)),
      on_packet_(std::move(other.on_packet_)),
      on_peer_connect_(std::move(other.on_peer_connect_)),
      on_peer_disconnect_(std::move(other.on_peer_disconnect_)) {
    other.eipc_ctx_ = nullptr;
}

BrokerManager& BrokerManager::operator=(BrokerManager&& other) noexcept {
    if (this != &other) {
        deinit();
        socket_path_ = std::move(other.socket_path_);
        eipc_ctx_ = other.eipc_ctx_;
        peers_ = std::move(other.peers_);
        on_packet_ = std::move(other.on_packet_);
        on_peer_connect_ = std::move(other.on_peer_connect_);
        on_peer_disconnect_ = std::move(other.on_peer_disconnect_);
        other.eipc_ctx_ = nullptr;
    }
    return *this;
}

bool BrokerManager::init(std::string_view socket_path) {
    socket_path_ = std::string(socket_path);

    /* Remove any existing dead socket file */
    unlink(socket_path_.c_str());

    eipc_config_t config{};
    config.socket_path = socket_path_.c_str();
    config.role = EIPC_ROLE_BROKER;
    config.callbacks.on_connect    = onConnectTrampoline;
    config.callbacks.on_disconnect = onDisconnectTrampoline;
    config.callbacks.on_msg        = onMsgTrampoline;
    config.callbacks.userdata      = this;

    eipc_ctx_ = eipc_create(&config);
    if (!eipc_ctx_) {
        std::fprintf(stderr, "[BrokerManager] Failed to create EIPC context\n");
        return false;
    }

    eipc_status_t status = eipc_init(eipc_ctx_);
    if (status != EIPC_OK) {
        std::fprintf(stderr, "[BrokerManager] Failed to init EIPC broker: %s\n", eipc_strerror(status));
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
        return false;
    }

    return true;
}

void BrokerManager::deinit() {
    if (eipc_ctx_) {
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
}

int BrokerManager::getEpollFd() const {
    return eipc_ctx_ ? eipc_get_epollfd(eipc_ctx_) : -1;
}

void BrokerManager::processEvents(uint32_t timeout_ms) {
    if (eipc_ctx_) {
        eipc_loop(eipc_ctx_, timeout_ms);
    }
}

bool BrokerManager::forwardMessage(int dest_peer_id, const eipc_message_t* msg) {
    if (!eipc_ctx_ || dest_peer_id < 0 || !msg) {
        return false;
    }
    return (eipc_send_message(eipc_ctx_, dest_peer_id, msg) == EIPC_OK);
}

void BrokerManager::broadcastMessage(const eipc_message_t* msg, int exclude_peer_id) {
    if (!eipc_ctx_ || !msg) {
        return;
    }
    for (const auto& peer : peers_) {
        if (peer.peer_id >= 0 && peer.peer_id != exclude_peer_id) {
            eipc_send_message(eipc_ctx_, peer.peer_id, msg);
        }
    }
}

bool BrokerManager::createDirectLink(int peer_id1, int peer_id2) {
    if (!eipc_ctx_ || peer_id1 < 0 || peer_id2 < 0) {
        return false;
    }
    return (eipc_create_direct_link(eipc_ctx_, peer_id1, peer_id2) == EIPC_OK);
}

proto_status_t BrokerManager::sendCmd(int peer_id,
                                      proto_service_id_t dst,
                                      uint16_t msg_id,
                                      uint16_t trans_id,
                                      const void* payload,
                                      size_t payload_len) {
    return proto_send_cmd(eipc_ctx_, peer_id, PROTO_SVC_MANAGER, dst, msg_id, trans_id, payload, payload_len);
}

proto_status_t BrokerManager::sendResp(int peer_id,
                                       proto_service_id_t dst,
                                       uint16_t msg_id,
                                       uint16_t trans_id,
                                       const void* payload,
                                       size_t payload_len) {
    return proto_send_resp(eipc_ctx_, peer_id, PROTO_SVC_MANAGER, dst, msg_id, trans_id, payload, payload_len);
}

proto_status_t BrokerManager::sendRespStatus(int peer_id,
                                             proto_service_id_t dst,
                                             uint16_t msg_id,
                                             uint16_t trans_id,
                                             proto_status_t status) {
    return proto_send_resp_status(eipc_ctx_, peer_id, PROTO_SVC_MANAGER, dst, msg_id, trans_id, status);
}

proto_status_t BrokerManager::sendEvent(int peer_id,
                                        proto_service_id_t dst,
                                        uint16_t msg_id,
                                        const void* payload,
                                        size_t payload_len) {
    return proto_send_event(eipc_ctx_, peer_id, PROTO_SVC_MANAGER, dst, msg_id, payload, payload_len);
}

proto_status_t BrokerManager::broadcastEvent(uint16_t msg_id,
                                             const void* payload,
                                             size_t payload_len) {
    proto_status_t last_status = PROTO_OK;
    for (const auto& p : peers_) {
        if (p.peer_id >= 0 && p.is_registered) {
            proto_status_t s = sendEvent(p.peer_id, p.service_id, msg_id, payload, payload_len);
            if (s != PROTO_OK) {
                last_status = s;
            }
        }
    }
    return last_status;
}

void BrokerManager::bindPeerService(int peer_id, proto_service_id_t svc_id, std::string_view name) {
    PeerConnection* p = findPeer(peer_id);
    if (p) {
        p->service_id = svc_id;
        p->service_name = std::string(name);
        p->is_registered = true;
    }
}

int BrokerManager::findPeerIdByService(proto_service_id_t svc_id) const {
    for (const auto& p : peers_) {
        if (p.peer_id >= 0 && p.service_id == svc_id && p.is_registered) {
            return p.peer_id;
        }
    }
    return -1;
}

PeerConnection* BrokerManager::findPeer(int peer_id) {
    for (auto& p : peers_) {
        if (p.peer_id == peer_id) {
            return &p;
        }
    }
    return nullptr;
}

const PeerConnection* BrokerManager::findPeer(int peer_id) const {
    for (const auto& p : peers_) {
        if (p.peer_id == peer_id) {
            return &p;
        }
    }
    return nullptr;
}

/* --- Static trampolines --- */

void BrokerManager::onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<BrokerManager*>(userdata);
    if (self) self->handleConnect(peer_id);
}

void BrokerManager::onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<BrokerManager*>(userdata);
    if (self) self->handleDisconnect(peer_id);
}

void BrokerManager::onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata) {
    (void)ctx;
    auto* self = static_cast<BrokerManager*>(userdata);
    if (self) self->handleMsg(peer_id, data);
}

void BrokerManager::handleConnect(int peer_id) {
    /* Insert into slot */
    for (auto& p : peers_) {
        if (p.peer_id < 0) {
            p.peer_id = peer_id;
            p.service_id = PROTO_SVC_UNKNOWN;
            p.is_registered = false;
            p.connected_time = std::chrono::steady_clock::now();
            break;
        }
    }

    if (on_peer_connect_) {
        on_peer_connect_(peer_id);
    }
}

void BrokerManager::handleDisconnect(int peer_id) {
    PeerConnection* p = findPeer(peer_id);
    if (p) {
        p->peer_id = -1;
        p->service_id = PROTO_SVC_UNKNOWN;
        p->is_registered = false;
    }

    if (on_peer_disconnect_) {
        on_peer_disconnect_(peer_id);
    }
}

void BrokerManager::handleMsg(int peer_id, const eipc_message_t* msg) {
    proto_packet_t pkt{};
    proto_status_t status = proto_unpack_packet(msg, &pkt);
    if (status != PROTO_OK) {
        std::fprintf(stderr, "[BrokerManager] Received malformed packet from peer %d\n", peer_id);
        if (msg && msg->num_fds > 0 && msg->fds != nullptr) {
            for (int i = 0; i < msg->num_fds; ++i) {
                if (msg->fds[i] >= 0) close(msg->fds[i]);
            }
        }
        return;
    }

    proto_service_id_t dst = static_cast<proto_service_id_t>(pkt.header.dst_id);

    /* Case 1: Targeted directly to Manager */
    if (dst == PROTO_SVC_MANAGER) {
        if (on_packet_) {
            on_packet_(peer_id, pkt);
        }
    }
    /* Case 2: Broadcast to all peers */
    else if (dst == PROTO_SVC_BROADCAST) {
        if (on_packet_) {
            on_packet_(peer_id, pkt);
        }
        broadcastMessage(msg, peer_id);
    }
    /* Case 3: Targeted to another service -> Forward (Proxy Routing) */
    else {
        int dest_peer_id = findPeerIdByService(dst);
        if (dest_peer_id >= 0) {
            forwardMessage(dest_peer_id, msg);
        } else {
            static auto last_warn_time = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_warn_time).count() >= 5) {
                std::fprintf(stderr, "[BrokerManager] Target service %s (dst: %d) not connected. Dropping.\n",
                             proto_service_name(dst), (int)dst);
                last_warn_time = now;
            }
        }
    }

    /* Prevent FD leak in broker: SCM_RIGHTS creates FDs in broker process when received.
     * When forwarded, the destination process gets its own FD. Broker must close its copy. */
    if (msg && msg->num_fds > 0 && msg->fds != nullptr) {
        for (int i = 0; i < msg->num_fds; ++i) {
            if (msg->fds[i] >= 0) {
                close(msg->fds[i]);
            }
        }
    }
}

} // namespace uav::manager
