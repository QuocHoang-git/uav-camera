#include "manager/system_monitor.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

namespace uav::manager {

SystemMonitor::SystemMonitor(std::string storage_path, std::string thermal_path)
    : storage_path_(std::move(storage_path)),
      thermal_path_(std::move(thermal_path)) {
    /* Prime initial CPU ticks */
    calculateCpuUsage();
}

proto_sys_metrics_resp_t SystemMonitor::collectMetrics() {
    proto_sys_metrics_resp_t resp{};
    resp.status = (int32_t)PROTO_OK;
    resp.cpu_temp_c = readTemperature();
    resp.cpu_usage_pct = calculateCpuUsage();
    readMemory(resp.mem_usage_pct, resp.mem_free_kb);
    resp.storage_free_mb = readStorageFreeMb();
    resp.uptime_sec = readUptimeSec();
    return resp;
}

int16_t SystemMonitor::readTemperature() {
    std::ifstream file(thermal_path_);
    if (!file.is_open()) {
        return 0;
    }

    int32_t millidegrees = 0;
    if (file >> millidegrees) {
        return static_cast<int16_t>(millidegrees / 1000);
    }
    return 0;
}

uint8_t SystemMonitor::calculateCpuUsage() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return 0;
    }

    std::string line;
    if (!std::getline(file, line) || line.compare(0, 3, "cpu") != 0) {
        return 0;
    }

    std::istringstream ss(line);
    std::string cpu_label;
    uint64_t user = 0, nice = 0, system = 0, idle = 0;
    uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;

    ss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    uint64_t idle_ticks = idle + iowait;
    uint64_t total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;

    if (prev_total_ticks_ == 0) {
        prev_idle_ticks_ = idle_ticks;
        prev_total_ticks_ = total_ticks;
        return 0;
    }

    uint64_t delta_idle = idle_ticks - prev_idle_ticks_;
    uint64_t delta_total = total_ticks - prev_total_ticks_;

    prev_idle_ticks_ = idle_ticks;
    prev_total_ticks_ = total_ticks;

    if (delta_total == 0) {
        return 0;
    }

    uint64_t busy = (delta_total > delta_idle) ? (delta_total - delta_idle) : 0;
    uint32_t usage = static_cast<uint32_t>((busy * 100) / delta_total);
    return static_cast<uint8_t>(usage > 100 ? 100 : usage);
}

void SystemMonitor::readMemory(uint8_t& out_usage_pct, uint32_t& out_free_kb) {
    out_usage_pct = 0;
    out_free_kb = 0;

    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        return;
    }

    std::string line;
    uint64_t mem_total_kb = 0;
    uint64_t mem_avail_kb = 0;

    while (std::getline(file, line)) {
        if (line.compare(0, 9, "MemTotal:") == 0) {
            std::sscanf(line.c_str(), "MemTotal: %lu kB", &mem_total_kb);
        } else if (line.compare(0, 13, "MemAvailable:") == 0) {
            std::sscanf(line.c_str(), "MemAvailable: %lu kB", &mem_avail_kb);
        }

        if (mem_total_kb > 0 && mem_avail_kb > 0) {
            break;
        }
    }

    out_free_kb = static_cast<uint32_t>(mem_avail_kb);
    if (mem_total_kb > 0 && mem_total_kb >= mem_avail_kb) {
        uint64_t used = mem_total_kb - mem_avail_kb;
        out_usage_pct = static_cast<uint8_t>((used * 100) / mem_total_kb);
    }
}

uint32_t SystemMonitor::readStorageFreeMb() {
    struct statvfs stat{};
    if (statvfs(storage_path_.c_str(), &stat) != 0) {
        return 0;
    }

    uint64_t free_bytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
    return static_cast<uint32_t>(free_bytes / (1024 * 1024));
}

uint32_t SystemMonitor::readUptimeSec() {
    struct sysinfo s_info{};
    if (sysinfo(&s_info) == 0) {
        return static_cast<uint32_t>(s_info.uptime);
    }
    return 0;
}

bool SystemMonitor::isStorageCriticallyLow(uint32_t threshold_mb) const {
    struct statvfs stat{};
    if (statvfs(storage_path_.c_str(), &stat) != 0) {
        return false;
    }
    uint64_t free_bytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;
    uint32_t free_mb = static_cast<uint32_t>(free_bytes / (1024 * 1024));
    return (free_mb < threshold_mb);
}

bool SystemMonitor::isOverheating(int16_t threshold_c) const {
    SystemMonitor* non_const_this = const_cast<SystemMonitor*>(this);
    return (non_const_this->readTemperature() >= threshold_c);
}

} // namespace uav::manager
