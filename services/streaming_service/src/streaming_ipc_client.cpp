#include "streaming/streaming_ipc_client.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace uav::streaming {

StreamingIpcClient::StreamingIpcClient() = default;

StreamingIpcClient::~StreamingIpcClient() {
    deinit();
}

StreamingIpcClient::StreamingIpcClient(StreamingIpcClient&& other) noexcept
    : socket_path_(std::move(other.socket_path_)),
      eipc_ctx_(other.eipc_ctx_),
      server_peer_id_(other.server_peer_id_),
      is_connected_(other.is_connected_),
      next_trans_id_(other.next_trans_id_),
      on_packet_(std::move(other.on_packet_)),
      on_connection_(std::move(other.on_connection_)) {
    other.eipc_ctx_ = nullptr;
    other.is_connected_ = false;
    other.server_peer_id_ = -1;
}

StreamingIpcClient& StreamingIpcClient::operator=(StreamingIpcClient&& other) noexcept {
    if (this != &other) {
        deinit();
        socket_path_ = std::move(other.socket_path_);
        eipc_ctx_ = other.eipc_ctx_;
        server_peer_id_ = other.server_peer_id_;
        is_connected_ = other.is_connected_;
        next_trans_id_ = other.next_trans_id_;
        on_packet_ = std::move(other.on_packet_);
        on_connection_ = std::move(other.on_connection_);

        other.eipc_ctx_ = nullptr;
        other.is_connected_ = false;
        other.server_peer_id_ = -1;
    }
    return *this;
}

bool StreamingIpcClient::init(std::string_view socket_path) {
    deinit();
    socket_path_ = std::string(socket_path);

    eipc_config_t config{};
    config.socket_path = socket_path_.c_str();
    config.role = EIPC_ROLE_CLIENT;
    config.callbacks.on_connect    = onConnectTrampoline;
    config.callbacks.on_disconnect = onDisconnectTrampoline;
    config.callbacks.on_msg        = onMsgTrampoline;
    config.callbacks.userdata      = this;

    eipc_ctx_ = eipc_create(&config);
    if (!eipc_ctx_) {
        std::fprintf(stderr, "[StreamingIpcClient] Failed to create EIPC client context\n");
        return false;
    }

    eipc_status_t status = eipc_init(eipc_ctx_);
    if (status != EIPC_OK) {
        std::fprintf(stderr, "[StreamingIpcClient] Failed to init EIPC client: %s\n", eipc_strerror(status));
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
        return false;
    }

    eipc_connect(eipc_ctx_);
    return true;
}

void StreamingIpcClient::deinit() {
    if (eipc_ctx_) {
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
    }
    is_connected_ = false;
    server_peer_id_ = -1;
}

int StreamingIpcClient::getEpollFd() const {
    return eipc_ctx_ ? eipc_get_epollfd(eipc_ctx_) : -1;
}

void StreamingIpcClient::processEvents(uint32_t timeout_ms) {
    if (eipc_ctx_) {
        eipc_loop(eipc_ctx_, timeout_ms);
    }
}

bool StreamingIpcClient::registerWithManager() {
    if (!eipc_ctx_ || server_peer_id_ < 0) {
        return false;
    }

    proto_sys_register_req_t req{};
    req.service_id = static_cast<uint8_t>(PROTO_SVC_STREAMING);
    req.pid = static_cast<uint32_t>(getpid());
    req.version_major = PROTO_VERSION_MAJOR;
    req.version_minor = PROTO_VERSION_MINOR;
    std::strncpy(req.service_name, "stream_srv", sizeof(req.service_name) - 1);

    proto_status_t st = proto_send_cmd(eipc_ctx_, server_peer_id_,
                                       PROTO_SVC_STREAMING, PROTO_SVC_MANAGER,
                                       PROTO_CMD_SYS_REGISTER, next_trans_id_++,
                                       &req, sizeof(req));
    return (st == PROTO_OK);
}

proto_status_t StreamingIpcClient::sendCommand(proto_service_id_t dst,
                                             uint16_t msg_id,
                                             uint16_t trans_id,
                                             const void* payload,
                                             size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_cmd(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, dst,
                          msg_id, trans_id, payload, payload_len);
}

proto_status_t StreamingIpcClient::sendResponse(proto_service_id_t dst,
                                              uint16_t msg_id,
                                              uint16_t trans_id,
                                              const void* payload,
                                              size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, dst,
                           msg_id, trans_id, payload, payload_len);
}

proto_status_t StreamingIpcClient::sendResponseStatus(proto_service_id_t dst,
                                                    uint16_t msg_id,
                                                    uint16_t trans_id,
                                                    proto_status_t status) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp_status(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, dst,
                                  msg_id, trans_id, status);
}

proto_status_t StreamingIpcClient::sendEvent(proto_service_id_t dst,
                                           uint16_t msg_id,
                                           const void* payload,
                                           size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, dst,
                            msg_id, payload, payload_len);
}

proto_status_t StreamingIpcClient::sendForceIdr(proto_service_id_t encoder_svc_id, uint8_t stream_id) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_frame_force_idr_req_t req{};
    req.stream_id = stream_id;

    return proto_send_cmd(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, encoder_svc_id,
                          PROTO_CMD_FRAME_FORCE_IDR, next_trans_id_++,
                          &req, sizeof(req));
}

proto_status_t StreamingIpcClient::sendFrameRelease(proto_service_id_t encoder_svc_id,
                                                  uint32_t buf_index,
                                                  uint32_t sequence) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_frame_release_req_t rel{};
    rel.buf_index = buf_index;
    rel.sequence  = sequence;

    return proto_send_cmd(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, encoder_svc_id,
                          PROTO_CMD_FRAME_RELEASE, next_trans_id_++,
                          &rel, sizeof(rel));
}

proto_status_t StreamingIpcClient::sendClientConnectedEvent(uint32_t client_id,
                                                         const std::string& ip,
                                                         uint16_t port,
                                                         uint8_t active_clients) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_stream_client_event_t ev{};
    ev.client_id = client_id;
    std::strncpy(ev.client_ip, ip.c_str(), sizeof(ev.client_ip) - 1);
    ev.client_port = port;
    ev.active_clients = active_clients;

    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, PROTO_SVC_MANAGER,
                            PROTO_EVENT_STREAM_CLIENT_CONNECTED, &ev, sizeof(ev));
}

proto_status_t StreamingIpcClient::sendClientDisconnectedEvent(uint32_t client_id,
                                                            const std::string& ip,
                                                            uint16_t port,
                                                            uint8_t active_clients) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_stream_client_event_t ev{};
    ev.client_id = client_id;
    std::strncpy(ev.client_ip, ip.c_str(), sizeof(ev.client_ip) - 1);
    ev.client_port = port;
    ev.active_clients = active_clients;

    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, PROTO_SVC_MANAGER,
                            PROTO_EVENT_STREAM_CLIENT_DISCONNECTED, &ev, sizeof(ev));
}

proto_status_t StreamingIpcClient::sendStreamStatsEvent(uint8_t active_clients,
                                                       uint32_t outgoing_kbps,
                                                       float packet_loss_pct,
                                                       uint64_t total_bytes) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_stream_stats_event_t ev{};
    ev.active_clients = active_clients;
    ev.outgoing_kbps = outgoing_kbps;
    ev.packet_loss_pct = packet_loss_pct;
    ev.total_bytes_sent = total_bytes;

    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_STREAMING, PROTO_SVC_MANAGER,
                            PROTO_EVENT_STREAM_STATS, &ev, sizeof(ev));
}

/* --- Static Trampolines --- */

void StreamingIpcClient::onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<StreamingIpcClient*>(userdata);
    if (self) self->handleConnect(peer_id);
}

void StreamingIpcClient::onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<StreamingIpcClient*>(userdata);
    if (self) self->handleDisconnect(peer_id);
}

void StreamingIpcClient::onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata) {
    (void)ctx;
    auto* self = static_cast<StreamingIpcClient*>(userdata);
    if (self) self->handleMsg(peer_id, data);
}

void StreamingIpcClient::handleConnect(int peer_id) {
    server_peer_id_ = peer_id;
    is_connected_ = true;
    std::printf("[StreamingIpcClient] Connected to broker (peer_id: %d). Registering...\n", peer_id);

    registerWithManager();

    if (on_connection_) {
        on_connection_(true);
    }
}

void StreamingIpcClient::handleDisconnect(int peer_id) {
    (void)peer_id;
    server_peer_id_ = -1;
    is_connected_ = false;
    std::fprintf(stderr, "[StreamingIpcClient] Disconnected from manager broker.\n");

    if (on_connection_) {
        on_connection_(false);
    }
}

void StreamingIpcClient::handleMsg(int peer_id, const eipc_message_t* msg) {
    (void)peer_id;
    if (!msg) return;

    proto_packet_t pkt{};
    proto_status_t status = proto_unpack_packet(msg, &pkt);
    if (status != PROTO_OK) {
        std::fprintf(stderr, "[StreamingIpcClient] Failed to unpack packet: %s\n", proto_status_str(status));
        if (msg->num_fds > 0 && msg->fds != nullptr) {
            for (int i = 0; i < msg->num_fds; ++i) {
                if (msg->fds[i] >= 0) close(msg->fds[i]);
            }
        }
        return;
    }

    if (on_packet_) {
        on_packet_(pkt);
    }
}

} // namespace uav::streaming
