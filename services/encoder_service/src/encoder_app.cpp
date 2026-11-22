#include "encoder/encoder_app.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <signal.h>
#include <errno.h>

namespace uav::encoder {

EncoderApp::EncoderApp(EncoderServiceConfig config)
    : config_(std::move(config)) {}

EncoderApp::~EncoderApp() {
    stop();

    if (signal_fd_ >= 0) {
        close(signal_fd_);
        signal_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EncoderApp::setupSignalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::perror("[EncoderApp] sigprocmask error");
        return false;
    }

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        std::perror("[EncoderApp] signalfd error");
        return false;
    }

    return true;
}

bool EncoderApp::setupEpoll() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::perror("[EncoderApp] epoll_create1 error");
        return false;
    }

    /* 1. Add signal_fd */
    struct epoll_event ev_sig{};
    ev_sig.events = EPOLLIN;
    ev_sig.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev_sig) < 0) {
        std::perror("[EncoderApp] epoll_ctl add signal_fd error");
        return false;
    }

    /* 2. Add IPC client epoll_fd */
    int ipc_fd = ipc_client_.getEpollFd();
    if (ipc_fd >= 0) {
        struct epoll_event ev_ipc{};
        ev_ipc.events = EPOLLIN;
        ev_ipc.data.fd = ipc_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ipc_fd, &ev_ipc) < 0) {
            std::perror("[EncoderApp] epoll_ctl add ipc_fd error");
            return false;
        }
    }

    /* 3. Add Encoder HAL event fd (if available) */
    int enc_fd = encoder_dev_.getFd();
    if (enc_fd >= 0) {
        struct epoll_event ev_enc{};
        ev_enc.events = EPOLLIN;
        ev_enc.data.fd = enc_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, enc_fd, &ev_enc) < 0) {
            std::perror("[EncoderApp] epoll_ctl add enc_fd error");
            return false;
        }
    }

    return true;
}

void EncoderApp::updateEncoderFdInEpoll(bool enable) {
    int enc_fd = encoder_dev_.getFd();
    if (enc_fd < 0 || epoll_fd_ < 0) return;

    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = enc_fd;

    if (enable) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, enc_fd, &ev);
    } else {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, enc_fd, nullptr);
    }
}

bool EncoderApp::init() {
    std::printf("[EncoderApp] Initializing UAV VPU Encoder Service...\n");

    if (!setupSignalfd()) {
        return false;
    }

    if (!encoder_dev_.init(config_)) {
        std::fprintf(stderr, "[EncoderApp] Failed to initialize hardware encoder device\n");
        return false;
    }

    if (!ipc_client_.init(config_.socket_path)) {
        std::fprintf(stderr, "[EncoderApp] Failed to initialize EIPC client\n");
        return false;
    }

    if (!setupEpoll()) {
        return false;
    }

    ipc_client_.setPacketHandler([this](const proto_packet_t& pkt) {
        handleIncomingIpcPacket(pkt);
    });

    if (config_.auto_start) {
        std::printf("[EncoderApp] Auto-starting hardware encoder pipeline...\n");
        encoder_dev_.start();
    }

    std::printf("[EncoderApp] UAV Encoder Service initialized successfully\n");
    return true;
}

int EncoderApp::run() {
    is_running_ = true;
    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (is_running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::perror("[EncoderApp] epoll_wait error");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handleSignalEvent();
            } else if (fd == ipc_client_.getEpollFd()) {
                ipc_client_.processEvents(0);
            } else if (fd == encoder_dev_.getFd()) {
                handleEncoderPacketReady();
            }
        }
    }

    std::printf("[EncoderApp] Encoder event loop terminated.\n");
    return 0;
}

void EncoderApp::stop() {
    is_running_ = false;
    encoder_dev_.stop();
}

void EncoderApp::handleSignalEvent() {
    struct signalfd_siginfo fdsi{};
    ssize_t s = read(signal_fd_, &fdsi, sizeof(fdsi));
    if (s != sizeof(fdsi)) return;

    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
        std::printf("\n[EncoderApp] Received signal %d. Stopping encoder and exiting...\n", fdsi.ssi_signo);
        stop();
    }
}

void EncoderApp::handleEncoderPacketReady() {
    hal_encoded_packet_t packet{};

    while (encoder_dev_.getPacket(packet, 0)) {
        proto_video_frame_meta_t meta{};
        meta.width        = config_.width;
        meta.height       = config_.height;
        meta.stride_w     = config_.width;
        meta.stride_h     = config_.height;
        meta.format       = (config_.codec == HAL_CODEC_H265) ? PROTO_CODEC_H265 : PROTO_CODEC_H264;
        meta.size_bytes   = packet.size;
        meta.buf_index    = packet.buf_index;
        meta.sequence     = packet.sequence;
        meta.timestamp_us = packet.timestamp_us;
        meta.frame_type   = packet.is_keyframe ? PROTO_FRAME_TYPE_ENCODED_IDR : PROTO_FRAME_TYPE_ENCODED_P;
        meta.is_keyframe  = packet.is_keyframe ? 1 : 0;
        meta.plane_count  = 1;
        meta.reserved     = 0;

        /* Distribute compressed packet to downstream streaming_service (Zero-Copy) */
        ipc_client_.sendEncodedFrame(config_.downstream_service, meta, packet.dma_fd);

        if (packet.dma_fd >= 0) {
            close(packet.dma_fd);
        }

        /* Note: Slot remains in_use until downstream streaming_service sends PROTO_CMD_FRAME_RELEASE */
    }
}

void EncoderApp::handleIncomingIpcPacket(const proto_packet_t& pkt) {
    uint16_t msg_id = pkt.header.msg_id;

    switch (msg_id) {
        case PROTO_EVENT_FRAME_RAW_AVAILABLE:
            dispatchRawFrame(pkt);
            break;

        case PROTO_CMD_FRAME_RELEASE:
            dispatchPacketRelease(pkt);
            break;

        case PROTO_CMD_ENC_START:
            dispatchStartEncoder(pkt);
            break;

        case PROTO_CMD_ENC_STOP:
            dispatchStopEncoder(pkt);
            break;

        case PROTO_CMD_ENC_GET_CONFIG:
            dispatchGetConfig(pkt);
            break;

        case PROTO_CMD_ENC_SET_CONFIG:
            dispatchSetConfig(pkt);
            break;

        case PROTO_CMD_ENC_SET_BITRATE:
            dispatchSetBitrate(pkt);
            break;

        case PROTO_CMD_ENC_GET_STATUS:
            dispatchGetStatus(pkt);
            break;

        case PROTO_CMD_FRAME_FORCE_IDR:
            dispatchForceIdr(pkt);
            break;

        case PROTO_CMD_SYS_PING:
            dispatchPing(pkt);
            break;

        case PROTO_RESP_SYS_REGISTER: {
            if (pkt.payload_len >= sizeof(proto_sys_register_resp_t)) {
                const auto* resp = reinterpret_cast<const proto_sys_register_resp_t*>(pkt.payload);
                std::printf("[EncoderApp] Manager registration status: %s (assigned ID: %d)\n",
                            proto_status_str(static_cast<proto_status_t>(resp->status)),
                            resp->assigned_id);
            }
            break;
        }

        case PROTO_EVENT_SYS_SERVICE_STATUS: {
            if (pkt.payload_len >= sizeof(proto_sys_service_status_event_t)) {
                const auto* ev = reinterpret_cast<const proto_sys_service_status_event_t*>(pkt.payload);
                std::printf("[EncoderApp] Service status update: %s is %s (pid: %u)\n",
                            proto_service_name((proto_service_id_t)ev->service_id),
                            ev->is_online ? "ONLINE" : "OFFLINE", ev->pid);
            }
            break;
        }

        default:
            break;
    }
}

void EncoderApp::dispatchRawFrame(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_video_frame_meta_t)) {
        if (pkt.num_fds > 0 && pkt.fds != nullptr) {
            for (int i = 0; i < pkt.num_fds; ++i) {
                if (pkt.fds[i] >= 0) close(pkt.fds[i]);
            }
        }
        return;
    }

    const auto* raw_meta = reinterpret_cast<const proto_video_frame_meta_t*>(pkt.payload);
    int dma_fd = (pkt.num_fds > 0) ? pkt.fds[0] : -1;

    hal_video_frame_t hal_frame{};
    hal_frame.width        = raw_meta->width;
    hal_frame.height       = raw_meta->height;
    hal_frame.stride_w     = raw_meta->stride_w;
    hal_frame.stride_h     = raw_meta->stride_h;
    hal_frame.format       = static_cast<hal_pixel_format_t>(raw_meta->format);
    hal_frame.size         = raw_meta->size_bytes;
    hal_frame.buf_index    = raw_meta->buf_index;
    hal_frame.sequence     = raw_meta->sequence;
    hal_frame.timestamp_us = raw_meta->timestamp_us;
    hal_frame.dma_fd       = dma_fd;

    /* Feed frame to hardware VPU */
    encoder_dev_.sendFrame(hal_frame, 0);

    /* Immediately release buffer slot back to camera_service */
    ipc_client_.sendFrameRelease(static_cast<proto_service_id_t>(pkt.header.src_id),
                                 raw_meta->buf_index,
                                 raw_meta->sequence);

    /* Close local copy of DMA-BUF FD */
    if (dma_fd >= 0) {
        close(dma_fd);
    }

    /* Drain any ready compressed packets */
    handleEncoderPacketReady();
}

void EncoderApp::dispatchStartEncoder(const proto_packet_t& pkt) {
    bool ok = encoder_dev_.start();
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_GENERIC;
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_ENC_START, pkt.header.trans_id, st);
}

void EncoderApp::dispatchStopEncoder(const proto_packet_t& pkt) {
    bool ok = encoder_dev_.stop();
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_GENERIC;
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_ENC_STOP, pkt.header.trans_id, st);
}

void EncoderApp::dispatchGetConfig(const proto_packet_t& pkt) {
    proto_enc_get_config_resp_t resp{};
    resp.status = static_cast<int32_t>(PROTO_OK);
    const auto& c = encoder_dev_.getConfig();
    resp.config.width        = c.width;
    resp.config.height       = c.height;
    resp.config.fps          = c.fps;
    resp.config.bitrate_kbps = c.bitrate_kbps;
    resp.config.codec        = (c.codec == HAL_CODEC_H265) ? PROTO_CODEC_H265 : PROTO_CODEC_H264;
    resp.config.rc_mode      = static_cast<uint8_t>(c.rc_mode);
    resp.config.profile      = static_cast<uint8_t>(c.profile);
    resp.config.gop_size     = static_cast<uint8_t>(c.gop_size);

    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_ENC_GET_CONFIG, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void EncoderApp::dispatchSetConfig(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_enc_config_t)) {
        ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                       PROTO_RESP_ENC_SET_CONFIG, pkt.header.trans_id,
                                       PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* new_cfg = reinterpret_cast<const proto_enc_config_t*>(pkt.payload);
    bool restart = encoder_dev_.isEncoding();
    if (restart) {
        encoder_dev_.stop();
    }

    EncoderServiceConfig cfg = encoder_dev_.getConfig();
    if (new_cfg->width > 0)        cfg.width = new_cfg->width;
    if (new_cfg->height > 0)       cfg.height = new_cfg->height;
    if (new_cfg->fps > 0)          cfg.fps = new_cfg->fps;
    if (new_cfg->bitrate_kbps > 0) cfg.bitrate_kbps = new_cfg->bitrate_kbps;
    if (new_cfg->gop_size > 0)     cfg.gop_size = new_cfg->gop_size;
    cfg.codec = (new_cfg->codec == PROTO_CODEC_H265) ? HAL_CODEC_H265 : HAL_CODEC_H264;

    bool ok = encoder_dev_.init(cfg);
    if (ok && restart) {
        encoder_dev_.start();
    }

    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_INVALID_ARG;
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_ENC_SET_CONFIG, pkt.header.trans_id, st);
}

void EncoderApp::dispatchSetBitrate(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_enc_set_bitrate_req_t)) {
        ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                       PROTO_RESP_ENC_SET_BITRATE, pkt.header.trans_id,
                                       PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* req = reinterpret_cast<const proto_enc_set_bitrate_req_t*>(pkt.payload);
    bool ok = encoder_dev_.setBitrate(req->target_bitrate_kbps);

    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_GENERIC;
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_ENC_SET_BITRATE, pkt.header.trans_id, st);
}

void EncoderApp::dispatchGetStatus(const proto_packet_t& pkt) {
    auto resp = encoder_dev_.getStatus();
    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_ENC_GET_STATUS, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void EncoderApp::dispatchForceIdr(const proto_packet_t& pkt) {
    bool ok = encoder_dev_.requestIdr();
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_GENERIC;
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_FRAME_FORCE_IDR, pkt.header.trans_id, st);
}

void EncoderApp::dispatchPacketRelease(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_frame_release_req_t)) return;
    const auto* rel = reinterpret_cast<const proto_frame_release_req_t*>(pkt.payload);
    encoder_dev_.releasePacketIndex(rel->buf_index);
}

void EncoderApp::dispatchPing(const proto_packet_t& pkt) {
    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_SYS_PONG, pkt.header.trans_id,
                             pkt.payload, pkt.payload_len);
}

} // namespace uav::encoder
