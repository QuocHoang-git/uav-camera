#include "manager/supervisor.hpp"
#include <cstring>
#include <algorithm>

namespace uav::manager {

Supervisor::Supervisor(uint32_t max_missed_heartbeats)
    : max_missed_heartbeats_(max_missed_heartbeats) {
    for (auto& s : services_) {
        s.service_id = PROTO_SVC_UNKNOWN;
        s.peer_id = -1;
        s.is_online = false;
    }
}

ServiceStatus* Supervisor::findServiceEntry(proto_service_id_t id) {
    for (auto& s : services_) {
        if (s.service_id == id) {
            return &s;
        }
    }
    return nullptr;
}

const ServiceStatus* Supervisor::findServiceEntry(proto_service_id_t id) const {
    for (const auto& s : services_) {
        if (s.service_id == id) {
            return &s;
        }
    }
    return nullptr;
}

ServiceStatus* Supervisor::findOrCreateEntry(proto_service_id_t id) {
    ServiceStatus* existing = findServiceEntry(id);
    if (existing) {
        return existing;
    }

    /* Find empty slot */
    for (auto& s : services_) {
        if (s.service_id == PROTO_SVC_UNKNOWN) {
            s.service_id = id;
            return &s;
        }
    }
    return nullptr;
}

proto_status_t Supervisor::registerService(const proto_sys_register_req_t& req, int peer_id) {
    proto_service_id_t svc_id = static_cast<proto_service_id_t>(req.service_id);
    if (svc_id == PROTO_SVC_UNKNOWN || svc_id == PROTO_SVC_MANAGER) {
        return PROTO_ERR_INVALID_ARG;
    }

    ServiceStatus* entry = findOrCreateEntry(svc_id);
    if (!entry) {
        return PROTO_ERR_NO_MEM;
    }

    entry->service_id = svc_id;
    entry->peer_id = peer_id;
    entry->pid = req.pid;
    entry->is_online = true;
    entry->missed_pings = 0;
    entry->last_heartbeat_time = std::chrono::steady_clock::now();
    entry->name = req.service_name;

    if (on_status_changed_) {
        on_status_changed_(svc_id, true, req.pid);
    }

    return PROTO_OK;
}

void Supervisor::unregisterByPeerId(int peer_id) {
    for (auto& s : services_) {
        if (s.peer_id == peer_id && s.is_online) {
            s.is_online = false;
            s.peer_id = -1;
            if (on_status_changed_) {
                on_status_changed_(s.service_id, false, s.pid);
            }
            break;
        }
    }
}

void Supervisor::unregisterByServiceId(proto_service_id_t service_id) {
    ServiceStatus* s = findServiceEntry(service_id);
    if (s && s->is_online) {
        s->is_online = false;
        s->peer_id = -1;
        if (on_status_changed_) {
            on_status_changed_(s->service_id, false, s->pid);
        }
    }
}

void Supervisor::handlePong(proto_service_id_t service_id, uint32_t seq) {
    (void)seq;
    ServiceStatus* s = findServiceEntry(service_id);
    if (s && s->is_online) {
        s->missed_pings = 0;
        s->last_heartbeat_time = std::chrono::steady_clock::now();
    }
}

void Supervisor::tickHeartbeat(PingSender ping_sender) {
    uint32_t seq = current_ping_seq_++;

    for (auto& s : services_) {
        if (!s.is_online || s.peer_id < 0) {
            continue;
        }

        s.missed_pings++;
        if (s.missed_pings >= max_missed_heartbeats_) {
            if (on_timeout_) {
                on_timeout_(s.service_id, s.missed_pings);
            }
        }

        if (ping_sender) {
            s.last_ping_seq = seq;
            ping_sender(s.peer_id, s.service_id, seq);
        }
    }
}

void Supervisor::handleChildExit(pid_t pid, int exit_status) {
    (void)exit_status;
    for (auto& s : services_) {
        if (s.pid == pid && s.is_online) {
            s.is_online = false;
            s.peer_id = -1;
            if (on_status_changed_) {
                on_status_changed_(s.service_id, false, pid);
            }
            break;
        }
    }
}

bool Supervisor::isServiceOnline(proto_service_id_t service_id) const {
    const ServiceStatus* s = findServiceEntry(service_id);
    return (s != nullptr && s->is_online);
}

bool Supervisor::areCoreServicesReady() const {
    return isServiceOnline(PROTO_SVC_CAMERA) && isServiceOnline(PROTO_SVC_STREAMING);
}

int Supervisor::getPeerId(proto_service_id_t service_id) const {
    const ServiceStatus* s = findServiceEntry(service_id);
    return (s && s->is_online) ? s->peer_id : -1;
}

std::optional<proto_service_id_t> Supervisor::getServiceId(int peer_id) const {
    for (const auto& s : services_) {
        if (s.peer_id == peer_id && s.is_online) {
            return s.service_id;
        }
    }
    return std::nullopt;
}

std::optional<ServiceStatus> Supervisor::getStatus(proto_service_id_t service_id) const {
    const ServiceStatus* s = findServiceEntry(service_id);
    if (s) {
        return *s;
    }
    return std::nullopt;
}

} // namespace uav::manager
