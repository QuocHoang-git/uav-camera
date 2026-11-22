#ifndef __UAV_MANAGER_SUPERVISOR_HPP
#define __UAV_MANAGER_SUPERVISOR_HPP

#include <cstdint>
#include <string_view>
#include <functional>
#include <array>
#include <optional>
#include <chrono>

#include "manager/types.hpp"
#include "protocol/proto_system.h"

namespace uav::manager {

/**
 * @brief Manages lifecycle, registration, and heartbeat tracking for child services
 */
class Supervisor {
public:
    using ServiceStatusHandler = std::function<void(proto_service_id_t service_id,
                                                    bool is_online,
                                                    pid_t pid)>;

    using ServiceTimeoutHandler = std::function<void(proto_service_id_t service_id,
                                                     uint32_t missed_count)>;

    using PingSender = std::function<bool(int peer_id, proto_service_id_t service_id, uint32_t seq)>;

    explicit Supervisor(uint32_t max_missed_heartbeats = DEFAULT_MAX_MISSED_HEARTBEATS);
    ~Supervisor() = default;

    /**
     * @brief Register a newly connected service announcing itself
     */
    proto_status_t registerService(const proto_sys_register_req_t& req, int peer_id);

    /**
     * @brief Handle client disconnection by peer ID
     */
    void unregisterByPeerId(int peer_id);

    /**
     * @brief Handle client disconnection by service ID
     */
    void unregisterByServiceId(proto_service_id_t service_id);

    /**
     * @brief Acknowledge a Pong response from a monitored service
     */
    void handlePong(proto_service_id_t service_id, uint32_t seq);

    /**
     * @brief Periodic heartbeat cycle (called at 1Hz)
     *
     * Sends Ping to all online services and detects unresponsive services.
     */
    void tickHeartbeat(PingSender ping_sender);

    /**
     * @brief Notify supervisor that a child process terminated (e.g. from SIGCHLD)
     */
    void handleChildExit(pid_t pid, int exit_status);

    /**
     * @brief Check if a specific service is currently online and responding
     */
    bool isServiceOnline(proto_service_id_t service_id) const;

    /**
     * @brief Check whether essential core services (Camera, Streaming) are ready
     */
    bool areCoreServicesReady() const;

    /**
     * @brief Find peer ID associated with a service
     */
    int getPeerId(proto_service_id_t service_id) const;

    /**
     * @brief Find service ID associated with a socket peer
     */
    std::optional<proto_service_id_t> getServiceId(int peer_id) const;

    /**
     * @brief Retrieve detailed status for a service
     */
    std::optional<ServiceStatus> getStatus(proto_service_id_t service_id) const;

    /**
     * @brief Set notification handler for service connect/disconnect events
     */
    void setServiceStatusHandler(ServiceStatusHandler handler) {
        on_status_changed_ = std::move(handler);
    }

    /**
     * @brief Set notification handler for heartbeat timeout events
     */
    void setTimeoutHandler(ServiceTimeoutHandler handler) {
        on_timeout_ = std::move(handler);
    }

private:
    uint32_t max_missed_heartbeats_;
    uint32_t current_ping_seq_ = 0;

    std::array<ServiceStatus, MAX_MANAGED_SERVICES> services_{};

    ServiceStatusHandler  on_status_changed_;
    ServiceTimeoutHandler on_timeout_;

    ServiceStatus* findServiceEntry(proto_service_id_t id);
    const ServiceStatus* findServiceEntry(proto_service_id_t id) const;
    ServiceStatus* findOrCreateEntry(proto_service_id_t id);
};

} // namespace uav::manager

#endif // __UAV_MANAGER_SUPERVISOR_HPP
