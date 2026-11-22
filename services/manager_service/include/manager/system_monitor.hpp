#ifndef __UAV_MANAGER_SYSTEM_MONITOR_HPP
#define __UAV_MANAGER_SYSTEM_MONITOR_HPP

#include <string>
#include <cstdint>
#include <chrono>

#include "manager/types.hpp"
#include "protocol/proto_system.h"

namespace uav::manager {

/**
 * @brief Hardware and system health monitor for Rockchip RV1106
 *
 * Reads CPU load, SoC temperature, RAM usage, storage space, and uptime
 * directly from Linux /proc and /sys subsystems with zero overhead.
 */
class SystemMonitor {
public:
    explicit SystemMonitor(std::string storage_path = std::string(DEFAULT_SD_MOUNT_POINT),
                           std::string thermal_path = std::string(DEFAULT_THERMAL_PATH));

    ~SystemMonitor() = default;

    /**
     * @brief Collect all current metrics into a protocol response struct
     */
    proto_sys_metrics_resp_t collectMetrics();

    /**
     * @brief Read CPU core temperature in Celsius
     */
    int16_t readTemperature();

    /**
     * @brief Calculate CPU utilization percentage (0 - 100%) since last call
     */
    uint8_t calculateCpuUsage();

    /**
     * @brief Query system RAM statistics from /proc/meminfo
     * @param[out] out_usage_pct Memory utilization percentage (0 - 100%)
     * @param[out] out_free_kb   Available memory in Kilobytes
     */
    void readMemory(uint8_t& out_usage_pct, uint32_t& out_free_kb);

    /**
     * @brief Query available free space on the recording media in Megabytes
     */
    uint32_t readStorageFreeMb();

    /**
     * @brief Get total system uptime in seconds
     */
    uint32_t readUptimeSec();

    /**
     * @brief Check if free storage is critically low
     */
    bool isStorageCriticallyLow(uint32_t threshold_mb = 50) const;

    /**
     * @brief Check if SoC is overheating
     */
    bool isOverheating(int16_t threshold_c = 85) const;

private:
    std::string storage_path_;
    std::string thermal_path_;

    /* Previous CPU tick counters for delta calculation */
    uint64_t prev_idle_ticks_  = 0;
    uint64_t prev_total_ticks_ = 0;
};

} // namespace uav::manager

#endif // __UAV_MANAGER_SYSTEM_MONITOR_HPP
