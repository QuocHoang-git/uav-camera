#ifndef __UAV_MANAGER_TYPES_HPP
#define __UAV_MANAGER_TYPES_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <chrono>
#include <array>
#include <optional>
#include <sys/types.h>

#include "protocol/protocol.h"

namespace uav::manager {

/* ========================================================================== */
/*                             Manager Constants                              */
/* ========================================================================== */

inline constexpr std::string_view DEFAULT_SOCKET_PATH     = "/tmp/uav_eipc.sock";
inline constexpr std::string_view DEFAULT_SD_MOUNT_POINT   = "/mnt/sd";
inline constexpr std::string_view DEFAULT_THERMAL_PATH     = "/sys/class/thermal/thermal_zone0/temp";

inline constexpr size_t MAX_CLIENT_PEERS                   = 16;
inline constexpr size_t MAX_MANAGED_SERVICES               = 8;

inline constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS   = 5000; /**< 1 Hz ping cycle */
inline constexpr uint32_t DEFAULT_MAX_MISSED_HEARTBEATS   = 3;    /**< 3 missed pings = dead */

/* ========================================================================== */
/*                           Configuration Settings                           */
/* ========================================================================== */

/**
 * @brief Runtime startup configuration for ManagerService
 */
struct ManagerConfig {
    std::string socket_path           = std::string(DEFAULT_SOCKET_PATH);
    std::string storage_path          = std::string(DEFAULT_SD_MOUNT_POINT);
    std::string thermal_sensor_path   = std::string(DEFAULT_THERMAL_PATH);
    uint32_t    heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
    uint32_t    max_missed_heartbeats = DEFAULT_MAX_MISSED_HEARTBEATS;
    bool        auto_restart_crashed  = true;
};

/* ========================================================================== */
/*                       Internal Tracking Structures                         */
/* ========================================================================== */

/**
 * @brief Tracks an active EIPC socket peer connection
 */
struct PeerConnection {
    int                peer_id          = -1;
    proto_service_id_t service_id       = PROTO_SVC_UNKNOWN;
    pid_t              pid              = 0;
    bool               is_registered    = false;
    std::string        service_name;
    std::chrono::steady_clock::time_point connected_time;
};

/**
 * @brief Lifecycle and heartbeat tracking entry for a registered service
 */
struct ServiceStatus {
    proto_service_id_t service_id       = PROTO_SVC_UNKNOWN;
    int                peer_id          = -1;
    pid_t              pid              = 0;
    bool               is_online        = false;
    uint32_t           missed_pings     = 0;
    uint32_t           last_ping_seq    = 0;
    std::string        name;
    std::chrono::steady_clock::time_point last_heartbeat_time;
};

} // namespace uav::manager

#endif // __UAV_MANAGER_TYPES_HPP
