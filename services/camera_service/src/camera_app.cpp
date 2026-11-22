#include "camera/camera_app.hpp"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace uav::camera {

CameraApp::CameraApp(CameraServiceConfig config)
    : config_(std::move(config)) {}

CameraApp::~CameraApp() {
    stop();
    if (epoll_fd_ >= 0) close(epoll_fd_);
    if (signal_fd_ >= 0) close(signal_fd_);
}

bool CameraApp::setupSignalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::perror("[CameraApp] sigprocmask failed");
        return false;
    }

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1) {
        std::perror("[CameraApp] signalfd failed");
        return false;
    }

    return true;
}

bool CameraApp::setupEpoll() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        std::perror("[CameraApp] epoll_create1 failed");
        return false;
    }

    struct epoll_event ev{};

    /* 1. Add signal_fd */
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) == -1) {
        std::perror("[CameraApp] epoll_ctl signal_fd failed");
        return false;
    }

    /* 2. Add IPC client epoll_fd */
    int ipc_fd = ipc_client_.getEpollFd();
    if (ipc_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = ipc_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ipc_fd, &ev) == -1) {
            std::perror("[CameraApp] epoll_ctl ipc_fd failed");
            return false;
        }
    }

    return true;
}

void CameraApp::updateCameraFdInEpoll(bool enable) {
    int cam_fd = camera_dev_.getFd();
    if (cam_fd < 0 || epoll_fd_ < 0) return;

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = cam_fd;

    if (enable) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, cam_fd, &ev) == -1) {
            /* If already added, modify */
            if (errno == EEXIST) {
                epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, cam_fd, &ev);
            } else {
                std::perror("[CameraApp] epoll_ctl add camera_fd failed");
            }
        }
    } else {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, cam_fd, nullptr);
    }
}

bool CameraApp::init() {
    std::printf("[CameraApp] Initializing UAV Camera Service...\n");

    if (!setupSignalfd()) {
        return false;
    }

    /* Initialize Camera hardware */
    if (!camera_dev_.init(config_)) {
        std::fprintf(stderr, "[CameraApp] WARNING: Camera device initialization deferred or failed\n");
    }

    /* Initialize EIPC connection to Manager */
    if (!ipc_client_.init(config_.socket_path)) {
        std::fprintf(stderr, "[CameraApp] Failed to init EIPC client to broker\n");
        return false;
    }

    if (!setupEpoll()) {
        return false;
    }

    /* Setup incoming message dispatcher */
    ipc_client_.setPacketHandler([this](const proto_packet_t& pkt) {
        handleIncomingIpcPacket(pkt);
    });

    if (config_.auto_start) {
        std::printf("[CameraApp] Auto-starting capture stream...\n");
        if (camera_dev_.start()) {
            updateCameraFdInEpoll(true);
        }
    }

    std::printf("[CameraApp] UAV Camera Service initialized successfully\n");
    return true;
}

int CameraApp::run() {
    is_running_ = true;
    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (is_running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::perror("[CameraApp] epoll_wait error");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handleSignalEvent();
            } else if (fd == ipc_client_.getEpollFd()) {
                ipc_client_.processEvents(0);
            } else if (fd == camera_dev_.getFd()) {
                handleCameraFrameReady();
            }
        }
    }

    std::printf("[CameraApp] Camera event loop terminated.\n");
    return 0;
}

void CameraApp::stop() {
    is_running_ = false;
    camera_dev_.stop();
    updateCameraFdInEpoll(false);
}

void CameraApp::handleSignalEvent() {
    struct signalfd_siginfo fdsi{};
    ssize_t s = read(signal_fd_, &fdsi, sizeof(fdsi));
    if (s != sizeof(fdsi)) return;

    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
        std::printf("\n[CameraApp] Received signal %d. Stopping capture and exiting...\n", fdsi.ssi_signo);
        stop();
    }
}

void CameraApp::handleCameraFrameReady() {
    hal_video_frame_t frame{};
    if (!camera_dev_.getFrame(frame, 0)) {
        return;
    }

    /* Construct zero-copy protocol frame metadata */
    proto_video_frame_meta_t meta{};
    meta.width        = frame.width;
    meta.height       = frame.height;
    meta.stride_w     = frame.stride_w;
    meta.stride_h     = frame.stride_h;
    meta.format       = frame.format;
    meta.size_bytes   = frame.size;
    meta.buf_index    = frame.buf_index;
    meta.sequence     = frame.sequence;
    meta.timestamp_us = frame.timestamp_us;
    meta.frame_type   = PROTO_FRAME_TYPE_RAW;
    meta.is_keyframe  = 1;
    meta.plane_count  = 2; /* NV12 */
    meta.reserved     = 0;

    /* Distribute DMA-BUF FD to hardware VPU encoder_service */
    ipc_client_.sendVideoFrame(PROTO_SVC_ENCODER, meta, frame.dma_fd);
}

void CameraApp::handleIncomingIpcPacket(const proto_packet_t& pkt) {
    uint16_t msg_id = pkt.header.msg_id;

    switch (msg_id) {
        case PROTO_CMD_CAM_START:
            dispatchStartCapture(pkt);
            break;

        case PROTO_CMD_CAM_STOP:
            dispatchStopCapture(pkt);
            break;

        case PROTO_CMD_CAM_GET_CONFIG:
            dispatchGetConfig(pkt);
            break;

        case PROTO_CMD_CAM_SET_CONFIG:
            dispatchSetConfig(pkt);
            break;

        case PROTO_CMD_CAM_GET_STATUS:
            dispatchGetStatus(pkt);
            break;

        case PROTO_CMD_FRAME_RELEASE:
            dispatchFrameRelease(pkt);
            break;

        case PROTO_CMD_SYS_PING:
            dispatchPing(pkt);
            break;

        case PROTO_RESP_SYS_REGISTER: {
            if (pkt.payload_len >= sizeof(proto_sys_register_resp_t)) {
                const auto* resp = reinterpret_cast<const proto_sys_register_resp_t*>(pkt.payload);
                std::printf("[CameraApp] Registration acknowledged by Manager (Status: %d, System State: %s)\n",
                            resp->status, proto_sys_state_str((proto_sys_state_t)resp->current_sys_state));
            }
            break;
        }

        case PROTO_EVENT_SYS_SERVICE_STATUS: {
            if (pkt.payload_len >= sizeof(proto_sys_service_status_event_t)) {
                const auto* ev = reinterpret_cast<const proto_sys_service_status_event_t*>(pkt.payload);
                std::printf("[CameraApp] Service status update: %s is %s (pid: %u)\n",
                            proto_service_name((proto_service_id_t)ev->service_id),
                            ev->is_online ? "ONLINE" : "OFFLINE", ev->pid);
            }
            break;
        }

        default:
            std::printf("[CameraApp] Unhandled message ID 0x%04X from sender %s\n",
                        msg_id, proto_service_name((proto_service_id_t)pkt.header.src_id));
            break;
    }
}

void CameraApp::dispatchStartCapture(const proto_packet_t& pkt) {
    std::printf("[CameraApp] Command received: START CAPTURE\n");
    bool ok = camera_dev_.start();
    if (ok) {
        updateCameraFdInEpoll(true);
    }
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_IO;
    ipc_client_.sendResponseStatus((proto_service_id_t)pkt.header.src_id,
                                   PROTO_RESP_CAM_START, pkt.header.trans_id, st);
}

void CameraApp::dispatchStopCapture(const proto_packet_t& pkt) {
    std::printf("[CameraApp] Command received: STOP CAPTURE\n");
    updateCameraFdInEpoll(false);
    bool ok = camera_dev_.stop();
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_IO;
    ipc_client_.sendResponseStatus((proto_service_id_t)pkt.header.src_id,
                                   PROTO_RESP_CAM_STOP, pkt.header.trans_id, st);
}

void CameraApp::dispatchGetConfig(const proto_packet_t& pkt) {
    proto_cam_get_config_resp_t resp{};
    resp.status = (int32_t)PROTO_OK;
    const auto& c = camera_dev_.getConfig();
    resp.config.width        = c.width;
    resp.config.height       = c.height;
    resp.config.fps          = c.fps;
    resp.config.bitrate_kbps = 4000;
    resp.config.codec        = PROTO_CODEC_H265;
    resp.config.pixel_format = PROTO_PIX_FMT_NV12;
    resp.config.gop_size     = 30;
    resp.config.rc_mode      = 0;

    ipc_client_.sendResponse((proto_service_id_t)pkt.header.src_id,
                             PROTO_RESP_CAM_GET_CONFIG, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void CameraApp::dispatchSetConfig(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_cam_config_t)) {
        ipc_client_.sendResponseStatus((proto_service_id_t)pkt.header.src_id,
                                       PROTO_RESP_CAM_SET_CONFIG, pkt.header.trans_id,
                                       PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* new_cfg = reinterpret_cast<const proto_cam_config_t*>(pkt.payload);
    std::printf("[CameraApp] Setting config: %ux%u @ %ufps\n",
                new_cfg->width, new_cfg->height, new_cfg->fps);

    bool restart = camera_dev_.isStreaming();
    if (restart) {
        camera_dev_.stop();
        updateCameraFdInEpoll(false);
    }

    config_.width = new_cfg->width;
    config_.height = new_cfg->height;
    config_.fps = new_cfg->fps;

    bool ok = camera_dev_.init(config_);
    if (ok && restart) {
        camera_dev_.start();
        updateCameraFdInEpoll(true);
    }

    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_INVALID_ARG;
    ipc_client_.sendResponseStatus((proto_service_id_t)pkt.header.src_id,
                                   PROTO_RESP_CAM_SET_CONFIG, pkt.header.trans_id, st);
}

void CameraApp::dispatchGetStatus(const proto_packet_t& pkt) {
    auto resp = camera_dev_.getStatus();
    ipc_client_.sendResponse((proto_service_id_t)pkt.header.src_id,
                             PROTO_RESP_CAM_GET_STATUS, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void CameraApp::dispatchFrameRelease(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_frame_release_req_t)) return;
    const auto* rel = reinterpret_cast<const proto_frame_release_req_t*>(pkt.payload);
    camera_dev_.releaseFrame(rel->buf_index);
}

void CameraApp::dispatchPing(const proto_packet_t& pkt) {
    ipc_client_.sendResponse((proto_service_id_t)pkt.header.src_id,
                             PROTO_RESP_SYS_PONG, pkt.header.trans_id,
                             pkt.payload, pkt.payload_len);
}

} // namespace uav::camera
