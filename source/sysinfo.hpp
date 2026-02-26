#ifndef SYSINFO_HPP
#define SYSINFO_HPP

#include <string>
#include <chrono>

/**
 * @class SysInfo
 * @brief Provides system-level information like CPU, RAM, and Uptime.
 */
class SysInfo {
public:
    SysInfo();

    /**
     * @brief Returns a formatted string with CPU usage, RAM usage, and Bot uptime.
     */
    std::string getStatusString() const;

private:
    std::chrono::steady_clock::time_point m_startTime;

    // CPU Snapshot state
    mutable long long m_lastTotalTicks = 0;
    mutable long long m_lastIdleTicks = 0;

    double getCpuUsage() const;
    void   getMemoryInfo(long long& total, long long& free) const;
    std::string getUptime() const;
};

#endif // SYSINFO_HPP
