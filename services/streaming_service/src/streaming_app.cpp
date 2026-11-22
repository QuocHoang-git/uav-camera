#include "streaming/streaming_app.hpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>

namespace uav::streaming {

StreamingApp::StreamingApp(StreamingServiceConfig config)
    : config_(std::move(config)),
      start_time_(std::chrono::steady_clock::now()),
      last_stats_time_(std::chrono::steady_clock::now()) {}

StreamingApp::~StreamingApp() {
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

bool StreamingApp::setupSignalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::perror("[StreamingApp] sigprocmask error");
        return false;
    }

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        std::perror("[StreamingApp] signalfd error");
        return false;
    }

    return true;
}

bool StreamingApp::setupEpoll() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        std::perror("[StreamingApp] epoll_create1 error");
        return false;
    }

    /* 1. Add signal_fd */
    struct epoll_event ev_sig{};
    ev_sig.events = EPOLLIN;
    ev_sig.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev_sig) < 0) {
        std::perror("[StreamingApp] epoll_ctl add signal_fd error");
        return false;
    }

    /* 2. Add IPC client epoll_fd */
    int ipc_fd = ipc_client_.getEpollFd();
    if (ipc_fd >= 0) {
        struct epoll_event ev_ipc{};
        ev_ipc.events = EPOLLIN;
        ev_ipc.data.fd = ipc_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ipc_fd, &ev_ipc) < 0) {
            std::perror("[StreamingApp] epoll_ctl add ipc_fd error");
            return false;
        }
    }

    return true;
}

bool StreamingApp::init() {
    std::printf("[StreamingApp] Initializing UAV Streaming Service...\n");

    if (!setupSignalfd()) {
        return false;
    }

    /* Instantiate RTSP Server */
    rtsp_server_ = std::make_unique<RtspServer>(config_.rtsp_port);
    rtsp_server_->addStream(config_.mount_point, config_.width, config_.height, config_.fps);

    /* Hook callbacks */
    rtsp_server_->setClientStateCallback([this](uint32_t client_id, const std::string& ip, uint16_t port, bool connected) {
        uint8_t active_clients = static_cast<uint8_t>(rtsp_server_->getActiveClientsCount());
        if (connected) {
            std::printf("[StreamingApp] Client joined: %s:%u (active: %u)\n", ip.c_str(), port, active_clients);
            ipc_client_.sendClientConnectedEvent(client_id, ip, port, active_clients);
        } else {
            std::printf("[StreamingApp] Client departed: %s:%u (active: %u)\n", ip.c_str(), port, active_clients);
            ipc_client_.sendClientDisconnectedEvent(client_id, ip, port, active_clients);
        }
    });

    rtsp_server_->setForceIdrCallback([this](const std::string& stream_path) {
        (void)stream_path;
        std::printf("[StreamingApp] Fast-start requested: triggering IDR keyframe from encoder...\n");
        ipc_client_.sendForceIdr(PROTO_SVC_ENCODER, 0);
    });

    /* Initialize EIPC connection to Manager */
    if (!ipc_client_.init(config_.socket_path)) {
        std::fprintf(stderr, "[StreamingApp] Failed to initialize EIPC client\n");
        return false;
    }

    if (!setupEpoll()) {
        return false;
    }

    ipc_client_.setPacketHandler([this](const proto_packet_t& pkt) {
        handleIncomingIpcPacket(pkt);
    });

    if (config_.auto_start) {
        std::printf("[StreamingApp] Auto-starting RTSP distribution server...\n");
        rtsp_server_->start();
    }

    start_time_ = std::chrono::steady_clock::now();
    last_stats_time_ = start_time_;
    last_bytes_sent_ = 0;

    std::printf("[StreamingApp] UAV Streaming Service initialized successfully on port %u\n", config_.rtsp_port);
    return true;
}

int StreamingApp::run() {
    is_running_ = true;
    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (is_running_) {
        /* Timeout 500ms to allow periodic stats dispatching */
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 500);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::perror("[StreamingApp] epoll_wait error");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handleSignalEvent();
            } else if (fd == ipc_client_.getEpollFd()) {
                ipc_client_.processEvents(0);
            }
        }

        checkAndSendStats();
    }

    std::printf("[StreamingApp] Streaming event loop terminated.\n");
    return 0;
}

void StreamingApp::stop() {
    is_running_ = false;
    if (rtsp_server_) {
        rtsp_server_->stop();
    }
}

void StreamingApp::handleSignalEvent() {
    struct signalfd_siginfo fdsi{};
    ssize_t s = read(signal_fd_, &fdsi, sizeof(fdsi));
    if (s != sizeof(fdsi)) return;

    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
        std::printf("\n[StreamingApp] Received signal %d. Stopping streaming service...\n", fdsi.ssi_signo);
        stop();
    }
}

void StreamingApp::checkAndSendStats() {
    if (!rtsp_server_ || !rtsp_server_->isRunning()) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time_).count();

    if (elapsed_ms >= 2000) { /* Every 2 seconds */
        uint8_t active_clients = static_cast<uint8_t>(rtsp_server_->getActiveClientsCount());
        uint64_t current_bytes = rtsp_server_->getTotalBytesSent();
        uint64_t delta_bytes = (current_bytes >= last_bytes_sent_) ? (current_bytes - last_bytes_sent_) : 0;

        uint32_t outgoing_kbps = static_cast<uint32_t>((delta_bytes * 8ULL * 1000ULL) / (elapsed_ms * 1024ULL));

        if (active_clients > 0 || current_bytes > 0) {
            ipc_client_.sendStreamStatsEvent(active_clients, outgoing_kbps, 0.0f, current_bytes);
        }

        last_stats_time_ = now;
        last_bytes_sent_ = current_bytes;
    }
}

void StreamingApp::handleIncomingIpcPacket(const proto_packet_t& pkt) {
    uint16_t msg_id = pkt.header.msg_id;

    switch (msg_id) {
        case PROTO_EVENT_FRAME_ENCODED_AVAILABLE:
            dispatchEncodedFrame(pkt);
            break;

        case PROTO_CMD_STREAM_START:
            dispatchStreamStart(pkt);
            break;

        case PROTO_CMD_STREAM_STOP:
            dispatchStreamStop(pkt);
            break;

        case PROTO_CMD_STREAM_GET_CONFIG:
            dispatchGetConfig(pkt);
            break;

        case PROTO_CMD_STREAM_SET_CONFIG:
            dispatchSetConfig(pkt);
            break;

        case PROTO_CMD_STREAM_GET_STATUS:
            dispatchGetStatus(pkt);
            break;

        case PROTO_CMD_SYS_PING:
            dispatchPing(pkt);
            break;

        case PROTO_RESP_SYS_REGISTER: {
            if (pkt.payload_len >= sizeof(proto_sys_register_resp_t)) {
                const auto* resp = reinterpret_cast<const proto_sys_register_resp_t*>(pkt.payload);
                std::printf("[StreamingApp] Manager registration status: %s (assigned ID: %d)\n",
                            proto_status_str(static_cast<proto_status_t>(resp->status)),
                            resp->assigned_id);
            }
            break;
        }

        case PROTO_EVENT_SYS_SERVICE_STATUS: {
            if (pkt.payload_len >= sizeof(proto_sys_service_status_event_t)) {
                const auto* ev = reinterpret_cast<const proto_sys_service_status_event_t*>(pkt.payload);
                std::printf("[StreamingApp] Service status update: %s is %s (pid: %u)\n",
                            proto_service_name(static_cast<proto_service_id_t>(ev->service_id)),
                            ev->is_online ? "ONLINE" : "OFFLINE", ev->pid);
            }
            break;
        }

        default:
            /* Close any dangling file descriptors */
            if (pkt.num_fds > 0 && pkt.fds != nullptr) {
                for (int i = 0; i < pkt.num_fds; ++i) {
                    if (pkt.fds[i] >= 0) close(pkt.fds[i]);
                }
            }
            break;
    }
}

void StreamingApp::dispatchEncodedFrame(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_video_frame_meta_t)) {
        if (pkt.num_fds > 0 && pkt.fds != nullptr) {
            for (int i = 0; i < pkt.num_fds; ++i) {
                if (pkt.fds[i] >= 0) close(pkt.fds[i]);
            }
        }
        return;
    }

    const auto* meta = reinterpret_cast<const proto_video_frame_meta_t*>(pkt.payload);
    int dma_fd = (pkt.num_fds > 0 && pkt.fds != nullptr) ? pkt.fds[0] : -1;

    if (dma_fd >= 0 && meta->size_bytes > 0) {
        void* mapped_ptr = mmap(nullptr, meta->size_bytes, PROT_READ, MAP_SHARED, dma_fd, 0);
        if (mapped_ptr != MAP_FAILED) {
            if (rtsp_server_ && rtsp_server_->isRunning()) {
                rtsp_server_->pushFrame(config_.mount_point,
                                        static_cast<const uint8_t*>(mapped_ptr),
                                        meta->size_bytes,
                                        meta->is_keyframe != 0);

                static uint32_t stream_pushed_frames = 0;
                static uint64_t stream_total_bytes = 0;
                static uint64_t total_stream_frames = 0;
                static auto last_stream_fps_time = std::chrono::steady_clock::now();

                stream_pushed_frames++;
                total_stream_frames++;
                stream_total_bytes += meta->size_bytes;
                if (stream_pushed_frames == 1) {
                    std::printf("[StreamingApp] First encoded frame pushed to RTSP! (size=%u bytes, keyframe=%s)\n",
                                meta->size_bytes, meta->is_keyframe ? "YES" : "NO");
                    std::fflush(stdout);
                }

                auto now = std::chrono::steady_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stream_fps_time).count();
                if (elapsed_ms >= 2000) {
                    float fps = (static_cast<float>(stream_pushed_frames) * 1000.0f) / static_cast<float>(elapsed_ms);
                    uint32_t kbps = static_cast<uint32_t>((stream_total_bytes * 8) / elapsed_ms);
                    std::printf("[StreamingApp] Stream Ingest: %.1f FPS, %u Kbps (total frames: %llu, active clients: %zu)\n",
                                fps, kbps, static_cast<unsigned long long>(total_stream_frames),
                                rtsp_server_ ? rtsp_server_->getActiveClientsCount() : 0);
                    std::fflush(stdout);
                    stream_pushed_frames = 0;
                    stream_total_bytes = 0;
                    last_stream_fps_time = now;
                }
            }
            munmap(mapped_ptr, meta->size_bytes);
        } else {
            std::perror("[StreamingApp] mmap dma_fd failed");
        }
        close(dma_fd);

        /* Return encoded packet buffer slot back to encoder_service */
        ipc_client_.sendFrameRelease(static_cast<proto_service_id_t>(pkt.header.src_id),
                                     meta->buf_index,
                                     meta->sequence);
    } else if (pkt.payload_len > sizeof(proto_video_frame_meta_t)) {
        /* Inline payload fallback */
        const uint8_t* bitstream = pkt.payload + sizeof(proto_video_frame_meta_t);
        size_t bitstream_len = pkt.payload_len - sizeof(proto_video_frame_meta_t);

        if (rtsp_server_ && rtsp_server_->isRunning()) {
            rtsp_server_->pushFrame(config_.mount_point, bitstream, bitstream_len, meta->is_keyframe != 0);
        }
    }

    /* Close any excess FDs to prevent leaks */
    if (pkt.num_fds > 1 && pkt.fds != nullptr) {
        for (int i = 1; i < pkt.num_fds; ++i) {
            if (pkt.fds[i] >= 0) close(pkt.fds[i]);
        }
    }
}

void StreamingApp::dispatchStreamStart(const proto_packet_t& pkt) {
    std::printf("[StreamingApp] Command received: START STREAM\n");
    bool ok = rtsp_server_ ? rtsp_server_->start() : false;
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_IO;

    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_STREAM_START, pkt.header.trans_id, st);
}

void StreamingApp::dispatchStreamStop(const proto_packet_t& pkt) {
    std::printf("[StreamingApp] Command received: STOP STREAM\n");
    if (rtsp_server_) {
        rtsp_server_->stop();
    }
    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_STREAM_STOP, pkt.header.trans_id, PROTO_OK);
}

void StreamingApp::dispatchGetConfig(const proto_packet_t& pkt) {
    proto_stream_get_config_resp_t resp{};
    resp.status = PROTO_OK;
    resp.config.port = config_.rtsp_port;
    resp.config.protocol = PROTO_STREAM_PROTO_RTSP_TCP;
    resp.config.max_clients = config_.max_clients;
    std::strncpy(resp.config.mount_point, config_.mount_point.c_str(), sizeof(resp.config.mount_point) - 1);
    std::snprintf(resp.stream_url, sizeof(resp.stream_url), "rtsp://0.0.0.0:%u%s",
                  config_.rtsp_port, config_.mount_point.c_str());

    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_STREAM_GET_CONFIG, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void StreamingApp::dispatchSetConfig(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_stream_config_t)) {
        ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                       PROTO_RESP_STREAM_SET_CONFIG, pkt.header.trans_id,
                                       PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* req = reinterpret_cast<const proto_stream_config_t*>(pkt.payload);
    if (req->port > 0) config_.rtsp_port = req->port;
    if (req->mount_point[0] != '\0') config_.mount_point = req->mount_point;
    if (req->max_clients > 0) config_.max_clients = req->max_clients;

    std::printf("[StreamingApp] Updated config: port=%u, mount_point=%s, max_clients=%u\n",
                config_.rtsp_port, config_.mount_point.c_str(), config_.max_clients);

    ipc_client_.sendResponseStatus(static_cast<proto_service_id_t>(pkt.header.src_id),
                                   PROTO_RESP_STREAM_SET_CONFIG, pkt.header.trans_id,
                                   PROTO_OK);
}

void StreamingApp::dispatchGetStatus(const proto_packet_t& pkt) {
    proto_stream_status_resp_t resp{};
    resp.status = PROTO_OK;
    resp.is_running = (rtsp_server_ && rtsp_server_->isRunning()) ? 1 : 0;
    resp.active_clients = rtsp_server_ ? static_cast<uint8_t>(rtsp_server_->getActiveClientsCount()) : 0;

    auto now = std::chrono::steady_clock::now();
    resp.uptime_sec = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count());
    std::snprintf(resp.stream_url, sizeof(resp.stream_url), "rtsp://0.0.0.0:%u%s",
                  config_.rtsp_port, config_.mount_point.c_str());

    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_STREAM_GET_STATUS, pkt.header.trans_id,
                             &resp, sizeof(resp));
}

void StreamingApp::dispatchPing(const proto_packet_t& pkt) {
    ipc_client_.sendResponse(static_cast<proto_service_id_t>(pkt.header.src_id),
                             PROTO_RESP_SYS_PONG, pkt.header.trans_id,
                             pkt.payload, pkt.payload_len);
}

} // namespace uav::streaming
