#include "manager/app.hpp"

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace uav::manager {

App::App(ManagerConfig config)
    : config_(std::move(config)),
      supervisor_(config_.max_missed_heartbeats),
      monitor_(config_.storage_path, config_.thermal_sensor_path) {}

App::~App() {
    stop();
    if (epoll_fd_ >= 0) close(epoll_fd_);
    if (signal_fd_ >= 0) close(signal_fd_);
    if (timer_fd_ >= 0) close(timer_fd_);
}

bool App::setupSignalfd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        std::perror("[App] sigprocmask failed");
        return false;
    }

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1) {
        std::perror("[App] signalfd failed");
        return false;
    }

    return true;
}

bool App::setupTimerfd() {
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd_ == -1) {
        std::perror("[App] timerfd_create failed");
        return false;
    }

    struct itimerspec its{};
    uint32_t interval_ms = config_.heartbeat_interval_ms;
    its.it_interval.tv_sec  = interval_ms / 1000;
    its.it_interval.tv_nsec = (interval_ms % 1000) * 1000000;
    its.it_value = its.it_interval;

    if (timerfd_settime(timer_fd_, 0, &its, nullptr) == -1) {
        std::perror("[App] timerfd_settime failed");
        return false;
    }

    return true;
}

bool App::setupEpoll() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        std::perror("[App] epoll_create1 failed");
        return false;
    }

    struct epoll_event ev{};

    /* 1. Add signal_fd */
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) == -1) {
        std::perror("[App] epoll_ctl signalfd failed");
        return false;
    }

    /* 2. Add timer_fd */
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev) == -1) {
        std::perror("[App] epoll_ctl timerfd failed");
        return false;
    }

    /* 3. Add broker epoll_fd */
    int broker_fd = broker_.getEpollFd();
    if (broker_fd >= 0) {
        ev.events = EPOLLIN;
        ev.data.fd = broker_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, broker_fd, &ev) == -1) {
            std::perror("[App] epoll_ctl broker_fd failed");
            return false;
        }
    }

    return true;
}

bool App::init() {
    std::printf("[App] Initializing UAV Manager Service...\n");

    if (!setupSignalfd() || !setupTimerfd()) {
        return false;
    }

    if (!broker_.init(config_.socket_path)) {
        return false;
    }

    if (!setupEpoll()) {
        return false;
    }

    /* Wire callbacks */
    broker_.setPacketHandler([this](int peer_id, const proto_packet_t& pkt) {
        handleIncomingPacket(peer_id, pkt);
    });

    broker_.setPeerDisconnectHandler([this](int peer_id) {
        handlePeerDisconnected(peer_id);
    });

    fsm_.setStateChangeHandler([this](proto_sys_state_t old_st, proto_sys_state_t new_st, uint16_t reason) {
        onFsmStateChanged(old_st, new_st, reason);
    });

    supervisor_.setServiceStatusHandler([this](proto_service_id_t svc_id, bool is_online, pid_t pid) {
        proto_sys_service_status_event_t ev{};
        ev.service_id = (uint8_t)svc_id;
        ev.is_online = is_online ? 1 : 0;
        ev.pid = (uint32_t)pid;
        broker_.broadcastEvent(PROTO_EVENT_SYS_SERVICE_STATUS, &ev, sizeof(ev));
    });

    supervisor_.setTimeoutHandler([this](proto_service_id_t svc_id, uint32_t missed_count) {
        std::fprintf(stderr, "[App] WARNING: Service %s (id: %d) missed %u heartbeats!\n",
                     proto_service_name(svc_id), (int)svc_id, missed_count);
        if (svc_id == PROTO_SVC_CAMERA && fsm_.isStreaming()) {
            fsm_.transitionTo(SYS_STATE_ERROR, 0xEE01, true);
        }
    });

    std::printf("[App] UAV Manager Service ready. Listening on socket: %s\n", config_.socket_path.c_str());
    return true;
}

int App::run() {
    is_running_ = true;
    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (is_running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            std::perror("[App] epoll_wait error");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handleSignalEvent();
            } else if (fd == timer_fd_) {
                handleTimerTick();
            } else if (fd == broker_.getEpollFd()) {
                broker_.processEvents(0);
            }
        }
    }

    std::printf("[App] Manager event loop terminated gracefully.\n");
    return 0;
}

void App::stop() {
    is_running_ = false;
}

void App::handleSignalEvent() {
    struct signalfd_siginfo fdsi{};
    ssize_t s = read(signal_fd_, &fdsi, sizeof(fdsi));
    if (s != sizeof(fdsi)) return;

    if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
        std::printf("\n[App] Received termination signal (%d). Shutting down...\n", fdsi.ssi_signo);
        fsm_.transitionTo(SYS_STATE_SHUTDOWN, 0, true);
        stop();
    } else if (fdsi.ssi_signo == SIGCHLD) {
        int status = 0;
        pid_t pid = 0;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            std::printf("[App] Child process %d terminated (exit: %d)\n", pid, status);
            supervisor_.handleChildExit(pid, status);
        }
    }
}

void App::handleTimerTick() {
    uint64_t expirations = 0;
    ssize_t s = read(timer_fd_, &expirations, sizeof(expirations));
    (void)s;

    /* 1. Tick heartbeats to monitored services */
    supervisor_.tickHeartbeat([this](int peer_id, proto_service_id_t svc_id, uint32_t seq) {
        proto_sys_ping_pong_t ping_data{};
        ping_data.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        ping_data.seq = seq;
        return (broker_.sendCmd(peer_id, svc_id, PROTO_CMD_SYS_PING,
                                (uint16_t)(seq & 0xFFFF), &ping_data, sizeof(ping_data)) == PROTO_OK);
    });

    /* 2. Check FSM auto-transition from INIT to STANDBY */
    if (fsm_.getCurrentState() == SYS_STATE_INIT && supervisor_.areCoreServicesReady()) {
        std::printf("[App] Core services (Camera & Streaming) are ready. Transitioning to STANDBY.\n");
        fsm_.transitionTo(SYS_STATE_STANDBY);
    }

    /* 3. Overheat detection */
    if (monitor_.isOverheating()) {
        std::fprintf(stderr, "[App] CRITICAL: SoC overheating! Temperature: %d C\n", monitor_.readTemperature());
    }
}

void App::handleIncomingPacket(int peer_id, const proto_packet_t& pkt) {
    uint16_t msg_id = pkt.header.msg_id;

    switch (msg_id) {
        case PROTO_CMD_SYS_PING: {
            /* Echo Pong back with same trans_id */
            broker_.sendResp(peer_id, (proto_service_id_t)pkt.header.src_id,
                             PROTO_RESP_SYS_PONG, pkt.header.trans_id,
                             pkt.payload, pkt.payload_len);
            break;
        }

        case PROTO_RESP_SYS_PONG: {
            supervisor_.handlePong((proto_service_id_t)pkt.header.src_id, pkt.header.trans_id);
            break;
        }

        case PROTO_CMD_SYS_REGISTER:
            dispatchRegister(peer_id, pkt);
            break;

        case PROTO_CMD_SYS_GET_STATE:
            dispatchGetState(peer_id, pkt);
            break;

        case PROTO_CMD_SYS_SET_STATE:
            dispatchSetState(peer_id, pkt);
            break;

        case PROTO_CMD_SYS_GET_METRICS:
            dispatchGetMetrics(peer_id, pkt);
            break;

        case PROTO_CMD_SYS_REQUEST_DIRECT_LINK:
            dispatchDirectLinkReq(peer_id, pkt);
            break;

        case PROTO_EVENT_TELEM_BATTERY:
            dispatchBatteryEvent(pkt);
            break;

        default:
            std::printf("[App] Unhandled message ID 0x%04X from peer %d\n", msg_id, peer_id);
            break;
    }
}

void App::handlePeerDisconnected(int peer_id) {
    std::printf("[App] Client disconnected on peer %d\n", peer_id);
    supervisor_.unregisterByPeerId(peer_id);
}

void App::dispatchRegister(int peer_id, const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_sys_register_req_t)) {
        broker_.sendRespStatus(peer_id, PROTO_SVC_UNKNOWN, PROTO_RESP_SYS_REGISTER, pkt.header.trans_id, PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* req = reinterpret_cast<const proto_sys_register_req_t*>(pkt.payload);
    proto_status_t st = supervisor_.registerService(*req, peer_id);

    proto_sys_register_resp_t resp{};
    resp.status = (int32_t)st;
    resp.assigned_id = req->service_id;
    resp.current_sys_state = (uint8_t)fsm_.getCurrentState();

    if (st == PROTO_OK) {
        broker_.bindPeerService(peer_id, (proto_service_id_t)req->service_id, req->service_name);
        std::printf("[App] Registered service: %s (id: %d, pid: %d, peer: %d)\n",
                    req->service_name, (int)req->service_id, (int)req->pid, peer_id);
    }

    broker_.sendResp(peer_id, (proto_service_id_t)req->service_id, PROTO_RESP_SYS_REGISTER, pkt.header.trans_id, &resp, sizeof(resp));
}

void App::dispatchGetState(int peer_id, const proto_packet_t& pkt) {
    auto resp = fsm_.getStatusResponse();
    broker_.sendResp(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_GET_STATE, pkt.header.trans_id, &resp, sizeof(resp));
}

void App::dispatchSetState(int peer_id, const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_sys_set_state_req_t)) {
        broker_.sendRespStatus(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_SET_STATE, pkt.header.trans_id, PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* req = reinterpret_cast<const proto_sys_set_state_req_t*>(pkt.payload);
    proto_status_t st = fsm_.transitionTo((proto_sys_state_t)req->target_state, req->reason_code, (req->force != 0));

    broker_.sendRespStatus(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_SET_STATE, pkt.header.trans_id, st);
}

void App::dispatchGetMetrics(int peer_id, const proto_packet_t& pkt) {
    auto resp = monitor_.collectMetrics();
    broker_.sendResp(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_GET_METRICS, pkt.header.trans_id, &resp, sizeof(resp));
}

void App::dispatchDirectLinkReq(int peer_id, const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_sys_direct_link_req_t)) {
        broker_.sendRespStatus(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_REQUEST_DIRECT_LINK, pkt.header.trans_id, PROTO_ERR_BAD_PAYLOAD);
        return;
    }

    const auto* req = reinterpret_cast<const proto_sys_direct_link_req_t*>(pkt.payload);
    int target_peer = broker_.findPeerIdByService((proto_service_id_t)req->target_service_id);

    if (target_peer < 0) {
        broker_.sendRespStatus(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_REQUEST_DIRECT_LINK, pkt.header.trans_id, PROTO_ERR_NOT_FOUND);
        return;
    }

    bool ok = broker_.createDirectLink(peer_id, target_peer);
    proto_status_t st = ok ? PROTO_OK : PROTO_ERR_IO;
    broker_.sendRespStatus(peer_id, (proto_service_id_t)pkt.header.src_id, PROTO_RESP_SYS_REQUEST_DIRECT_LINK, pkt.header.trans_id, st);
}

void App::dispatchBatteryEvent(const proto_packet_t& pkt) {
    if (pkt.payload_len < sizeof(proto_telem_battery_event_t)) return;
    const auto* b = reinterpret_cast<const proto_telem_battery_event_t*>(pkt.payload);

    if (b->is_critical) {
        std::fprintf(stderr, "[App] CRITICAL BATTERY ALERT (%u%%). Entering LOW_POWER and commanding safe shutdown.\n", b->remaining_pct);
        fsm_.transitionTo(SYS_STATE_LOW_POWER, 0xBA77, true);

        /* Instruct Camera to safely stop recording */
        int cam_peer = broker_.findPeerIdByService(PROTO_SVC_CAMERA);
        if (cam_peer >= 0) {
            broker_.sendCmd(cam_peer, PROTO_SVC_CAMERA, PROTO_CMD_CAM_STOP_RECORD, 0xFFFF);
        }
    }
}

void App::onFsmStateChanged(proto_sys_state_t old_st, proto_sys_state_t new_st, uint16_t reason) {
    std::printf("[App] System State Changed: %s -> %s (Reason: 0x%04X)\n",
                proto_sys_state_str(old_st), proto_sys_state_str(new_st), reason);

    proto_sys_state_changed_event_t ev{};
    ev.old_state = (uint8_t)old_st;
    ev.new_state = (uint8_t)new_st;
    ev.reason_code = reason;
    ev.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    broker_.broadcastEvent(PROTO_EVENT_SYS_STATE_CHANGED, &ev, sizeof(ev));
}

} // namespace uav::manager
