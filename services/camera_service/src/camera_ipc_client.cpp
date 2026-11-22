#include "camera/camera_ipc_client.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>

namespace uav::camera {

CameraIpcClient::CameraIpcClient() = default;

CameraIpcClient::~CameraIpcClient() {
    deinit();
}

CameraIpcClient::CameraIpcClient(CameraIpcClient&& other) noexcept
    : socket_path_(std::move(other.socket_path_)),
      eipc_ctx_(other.eipc_ctx_),
      server_peer_id_(other.server_peer_id_),
      is_connected_(other.is_connected_),
      on_packet_(std::move(other.on_packet_)),
      on_connection_(std::move(other.on_connection_)) {
    other.eipc_ctx_ = nullptr;
    other.is_connected_ = false;
    other.server_peer_id_ = -1;
}

CameraIpcClient& CameraIpcClient::operator=(CameraIpcClient&& other) noexcept {
    if (this != &other) {
        deinit();
        socket_path_ = std::move(other.socket_path_);
        eipc_ctx_ = other.eipc_ctx_;
        server_peer_id_ = other.server_peer_id_;
        is_connected_ = other.is_connected_;
        on_packet_ = std::move(other.on_packet_);
        on_connection_ = std::move(other.on_connection_);

        other.eipc_ctx_ = nullptr;
        other.is_connected_ = false;
        other.server_peer_id_ = -1;
    }
    return *this;
}

bool CameraIpcClient::init(std::string_view socket_path) {
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
        std::fprintf(stderr, "[CameraIpcClient] Failed to create EIPC client context\n");
        return false;
    }

    eipc_status_t status = eipc_init(eipc_ctx_);
    if (status != EIPC_OK) {
        std::fprintf(stderr, "[CameraIpcClient] Failed to init EIPC client: %s\n", eipc_strerror(status));
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
        return false;
    }

    /* Initiate non-blocking connection */
    eipc_connect(eipc_ctx_);
    return true;
}

void CameraIpcClient::deinit() {
    if (eipc_ctx_) {
        eipc_destroy(eipc_ctx_);
        eipc_ctx_ = nullptr;
    }
    is_connected_ = false;
    server_peer_id_ = -1;
}

int CameraIpcClient::getEpollFd() const {
    return eipc_ctx_ ? eipc_get_epollfd(eipc_ctx_) : -1;
}

void CameraIpcClient::processEvents(uint32_t timeout_ms) {
    if (eipc_ctx_) {
        eipc_loop(eipc_ctx_, timeout_ms);
    }
}

bool CameraIpcClient::registerWithManager() {
    if (!eipc_ctx_ || server_peer_id_ < 0) {
        return false;
    }

    proto_sys_register_req_t req{};
    req.service_id = static_cast<uint8_t>(PROTO_SVC_CAMERA);
    req.pid = static_cast<uint32_t>(getpid());
    req.version_major = PROTO_VERSION_MAJOR;
    req.version_minor = PROTO_VERSION_MINOR;
    std::strncpy(req.service_name, "cam_srv", sizeof(req.service_name) - 1);

    proto_status_t st = proto_send_cmd(eipc_ctx_, server_peer_id_,
                                       PROTO_SVC_CAMERA, PROTO_SVC_MANAGER,
                                       PROTO_CMD_SYS_REGISTER, 0x0001,
                                       &req, sizeof(req));
    return (st == PROTO_OK);
}

proto_status_t CameraIpcClient::sendResponse(proto_service_id_t dst,
                                             uint16_t msg_id,
                                             uint16_t trans_id,
                                             const void* payload,
                                             size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp(eipc_ctx_, server_peer_id_, PROTO_SVC_CAMERA, dst, msg_id, trans_id, payload, payload_len);
}

proto_status_t CameraIpcClient::sendResponseStatus(proto_service_id_t dst,
                                                   uint16_t msg_id,
                                                   uint16_t trans_id,
                                                   proto_status_t status) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_resp_status(eipc_ctx_, server_peer_id_, PROTO_SVC_CAMERA, dst, msg_id, trans_id, status);
}

proto_status_t CameraIpcClient::sendEvent(proto_service_id_t dst,
                                          uint16_t msg_id,
                                          const void* payload,
                                          size_t payload_len) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_event(eipc_ctx_, server_peer_id_, PROTO_SVC_CAMERA, dst, msg_id, payload, payload_len);
}

proto_status_t CameraIpcClient::sendVideoFrame(proto_service_id_t dst,
                                               const proto_video_frame_meta_t& meta,
                                               int dma_fd) {
    if (!eipc_ctx_ || server_peer_id_ < 0) return PROTO_ERR_SERVICE_DOWN;
    return proto_send_video_frame(eipc_ctx_, server_peer_id_, PROTO_SVC_CAMERA, dst,
                                  PROTO_EVENT_FRAME_RAW_AVAILABLE, &meta, dma_fd);
}

/* --- Static Trampolines --- */

void CameraIpcClient::onConnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<CameraIpcClient*>(userdata);
    if (self) self->handleConnect(peer_id);
}

void CameraIpcClient::onDisconnectTrampoline(eipc_context_t* ctx, int peer_id, void* userdata) {
    (void)ctx;
    auto* self = static_cast<CameraIpcClient*>(userdata);
    if (self) self->handleDisconnect(peer_id);
}

void CameraIpcClient::onMsgTrampoline(eipc_context_t* ctx, int peer_id, const eipc_message_t* data, void* userdata) {
    (void)ctx;
    auto* self = static_cast<CameraIpcClient*>(userdata);
    if (self) self->handleMsg(peer_id, data);
}

void CameraIpcClient::handleConnect(int peer_id) {
    server_peer_id_ = peer_id;
    is_connected_ = true;
    std::printf("[CameraIpcClient] Connected to Manager Broker (peer: %d). Registering...\n", peer_id);
    registerWithManager();

    if (on_connection_) {
        on_connection_(true);
    }
}

void CameraIpcClient::handleDisconnect(int peer_id) {
    (void)peer_id;
    is_connected_ = false;
    server_peer_id_ = -1;
    std::printf("[CameraIpcClient] Disconnected from Manager Broker\n");

    if (on_connection_) {
        on_connection_(false);
    }
}

void CameraIpcClient::handleMsg(int peer_id, const eipc_message_t* msg) {
    (void)peer_id;
    proto_packet_t pkt{};
    proto_status_t status = proto_unpack_packet(msg, &pkt);
    if (status != PROTO_OK) {
        std::fprintf(stderr, "[CameraIpcClient] Received invalid protocol packet\n");
        return;
    }

    if (on_packet_) {
        on_packet_(pkt);
    }
}

} // namespace uav::camera
