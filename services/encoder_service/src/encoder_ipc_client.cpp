#include "encoder/encoder_ipc_client.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace uav::encoder {

EncoderIpcClient::EncoderIpcClient() = default;

EncoderIpcClient::~EncoderIpcClient() {
    deinit();
}

EncoderIpcClient::EncoderIpcClient(EncoderIpcClient&& other) noexcept
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

EncoderIpcClient& EncoderIpcClient::operator=(EncoderIpcClient&& other) noexcept {
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

bool EncoderIpcClient::init(std::string_view socket_path) {
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
        std::fprintf(stderr, "[EncoderIpcClient] Failed to create EIPC client context\n");
        return false;
    }

    eipc_status_t status = eipc_init(eipc_ctx_);
    if (status != EIPC_OK) {
        std::fprintf(stderr, "[EncoderIpcClient] Failed to init EIPC client: %s\n", eipc_strerror(status));
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
        return false;
    }

    /* Initiate non-blocking connection to manager_service broker */
    eipc_connect(eipc_ctx_);
    return true;
}

void EncoderIpcClient::deinit() {
    if (eipc_ctx_) {
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
    }
    is_connected_ = false;
    server_peer_id_ = -1;
}

int EncoderIpcClient::getEpollFd() const {
    return eipc_ctx_ ? eipc_get_epollfd(eipc_ctx_) : -1;
}

void EncoderIpcClient::processEvents(uint32_t timeout_ms) {
    if (eipc_ctx_) {
        eipc_loop(eipc_ctx_, timeout_ms);
    }
}

bool EncoderIpcClient::registerWithManager() {
    if (!eipc_ctx_ || server_peer_id_ < 0) {
        return false;
    }

    proto_sys_register_req_t req{};
    req.service_id = static_cast<uint8_t>(PROTO_SVC_ENCODER);
    req.pid = static_cast<uint32_t>(getpid());
    req.version_major = PROTO_VERSION_MAJOR;
    req.version_minor = PROTO_VERSION_MINOR;
    std::strncpy(req.service_name, "enc_srv", sizeof(req.service_name) - 1);

    proto_status_t st = proto_send_cmd(eipc_ctx_, server_peer_id_,
                                       PROTO_SVC_ENCODER, PROTO_SVC_MANAGER,
                                       PROTO_CMD_SYS_REGISTER, next_trans_id_++,
                                       &req, sizeof(req));
    return (st == PROTO_OK);
}

proto_status_t EncoderIpcClient::sendCommand(proto_service_id_t dst,
                                             uint16_t msg_id,
                                             uint16_t trans_id,
                                             const void* payload,
                                             size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_cmd(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, dst,
                          msg_id, trans_id, payload, payload_len);
}

proto_status_t EncoderIpcClient::sendResponse(proto_service_id_t dst,
                                              uint16_t msg_id,
                                              uint16_t trans_id,
                                              const void* payload,
                                              size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, dst,
                           msg_id, trans_id, payload, payload_len);
}

proto_status_t EncoderIpcClient::sendResponseStatus(proto_service_id_t dst,
                                                    uint16_t msg_id,
                                                    uint16_t trans_id,
                                                    proto_status_t status) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp_status(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, dst,
                                  msg_id, trans_id, status);
}

proto_status_t EncoderIpcClient::sendEvent(proto_service_id_t dst,
                                           uint16_t msg_id,
                                           const void* payload,
                                           size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, dst,
                            msg_id, payload, payload_len);
}

proto_status_t EncoderIpcClient::sendEncodedFrame(proto_service_id_t dst,
                                                  const proto_video_frame_meta_t& meta,
                                                  int dma_fd) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_video_frame(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, dst,
                                  PROTO_EVENT_FRAME_ENCODED_AVAILABLE, &meta, dma_fd);
}

proto_status_t EncoderIpcClient::sendFrameRelease(proto_service_id_t camera_svc_id,
                                                  uint32_t buf_index,
                                                  uint32_t sequence) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;

    proto_frame_release_req_t rel{};
    rel.buf_index = buf_index;
    rel.sequence  = sequence;

    return proto_send_cmd(eipc_ctx_, server_peer_id_, PROTO_SVC_ENCODER, camera_svc_id,
                          PROTO_CMD_FRAME_RELEASE, next_trans_id_++,
                          &rel, sizeof(rel));
}

/* --- Static Trampolines --- */

void EncoderIpcClient::onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<EncoderIpcClient*>(userdata);
    if (self) self->handleConnect(peer_id);
}

void EncoderIpcClient::onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<EncoderIpcClient*>(userdata);
    if (self) self->handleDisconnect(peer_id);
}

void EncoderIpcClient::onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata) {
    (void)ctx;
    auto* self = static_cast<EncoderIpcClient*>(userdata);
    if (self) self->handleMsg(peer_id, data);
}

void EncoderIpcClient::handleConnect(int peer_id) {
    server_peer_id_ = peer_id;
    is_connected_ = true;
    std::printf("[EncoderIpcClient] Connected to broker (peer_id: %d). Registering...\n", peer_id);

    registerWithManager();

    if (on_connection_) {
        on_connection_(true);
    }
}

void EncoderIpcClient::handleDisconnect(int peer_id) {
    (void)peer_id;
    server_peer_id_ = -1;
    is_connected_ = false;
    std::fprintf(stderr, "[EncoderIpcClient] Disconnected from manager broker.\n");

    if (on_connection_) {
        on_connection_(false);
    }
}

void EncoderIpcClient::handleMsg(int peer_id, const eipc_message_t* msg) {
    (void)peer_id;
    if (!msg) return;

    proto_packet_t pkt{};
    proto_status_t status = proto_unpack_packet(msg, &pkt);
    if (status != PROTO_OK) {
        std::fprintf(stderr, "[EncoderIpcClient] Failed to unpack packet: %s\n", proto_status_str(status));
        return;
    }

    if (on_packet_) {
        on_packet_(pkt);
    }
}

} // namespace uav::encoder
