#include "sysinfo.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <unistd.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

SysInfo::SysInfo() : m_startTime(std::chrono::steady_clock::now()) {}

std::string SysInfo::getUptime() const {
    auto now = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - m_startTime).count();

    int days = diff / (24 * 3600);
    diff %= (24 * 3600);
    int hours = diff / 3600;
    diff %= 3600;
    int minutes = diff / 60;
    int seconds = diff % 60;

    std::stringstream ss;
    if (days > 0) ss << days << "d ";
    ss << std::setfill('0') << std::setw(2) << hours << ":"
       << std::setw(2) << minutes << ":"
       << std::setw(2) << seconds;
    return ss.str();
}

double SysInfo::getCpuUsage() const {
    long long totalTicks = 0;
    long long idleTicks = 0;

#if defined(_WIN32)
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        auto to_long = [](const FILETIME& ft) {
            return (static_cast<long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        idleTicks = to_long(idleTime);
        totalTicks = to_long(kernelTime) + to_long(userTime);
    }
#elif defined(__linux__)
    std::ifstream file("/proc/stat");
    std::string line;
    if (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string cpu;
        long long user, nice, system, idle, iowait, irq, softirq, steal;
        ss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        idleTicks = idle + iowait;
        totalTicks = user + nice + system + idle + iowait + irq + softirq + steal;
    }
#elif defined(__APPLE__)
    host_cpu_load_info_data_t cpuLoad;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&cpuLoad, &count) == KERN_SUCCESS) {
        idleTicks = cpuLoad.cpu_ticks[CPU_STATE_IDLE];
        totalTicks = cpuLoad.cpu_ticks[CPU_STATE_USER] + 
                     cpuLoad.cpu_ticks[CPU_STATE_SYSTEM] + 
                     cpuLoad.cpu_ticks[CPU_STATE_NICE] + 
                     cpuLoad.cpu_ticks[CPU_STATE_IDLE];
    }
#endif

    double percent = 0.0;
    if (m_lastTotalTicks > 0) {
        long long totalDiff = totalTicks - m_lastTotalTicks;
        long long idleDiff  = idleTicks - m_lastIdleTicks;
        if (totalDiff > 0) {
            percent = 100.0 * (1.0 - static_cast<double>(idleDiff) / totalDiff);
        }
    }

    m_lastTotalTicks = totalTicks;
    m_lastIdleTicks = idleTicks;

    // Ensure it's in a readable range (0-100)
    return std::max(0.0, std::min(100.0, percent));
}

void SysInfo::getMemoryInfo(long long& total, long long& free) const {
#if defined(_WIN32)
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    total = memInfo.ullTotalPhys / (1024 * 1024);
    free = memInfo.ullAvailPhys / (1024 * 1024);
#elif defined(__linux__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        total = (info.totalram * info.mem_unit) / (1024 * 1024);
        free = (info.freeram * info.mem_unit) / (1024 * 1024);
    } else {
        total = 0; free = 0;
    }
#elif defined(__APPLE__)
    int64_t memsize;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, NULL, 0);
    total = memsize / (1024 * 1024);

    mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
    vm_statistics_data_t vm_stats;
    host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stats, &count);
    free = (static_cast<long long>(vm_stats.free_count) * sysconf(_SC_PAGESIZE)) / (1024 * 1024);
#else
    total = 0; free = 0;
#endif
}

std::string SysInfo::getStatusString() const {
    long long total, free;
    getMemoryInfo(total, free);
    long long used = total - free;

    std::stringstream ss;
    ss << "🖥 *System Status*\n"
       << "━━━━━━━━━━━━━━━\n"
       << "⏱ *Uptime*: `" << getUptime() << "`\n"
       << "📊 *CPU*: `" << std::fixed << std::setprecision(1) << getCpuUsage() << "%`\n"
       << "🧠 *RAM*: `" << used << "MB / " << total << "MB`\n"
       << "🚀 *Threads*: `Active`";
    return ss.str();
}
