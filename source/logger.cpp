#include "logger.hpp"
#include <chrono>
#include <ctime>

// Static member definitions
std::mutex    Logger::m_logMutex;
std::ofstream Logger::m_logFile;
Logger::Level Logger::m_minLevel = Logger::Level::INFO;

void Logger::configure(const std::string& filepath, Level level) {
    std::lock_guard<std::mutex> lock(m_logMutex);
    m_minLevel = level;
    if (!filepath.empty()) {
        m_logFile.open(filepath, std::ios::app);
    }
}

std::string Logger::timestamp() {
    auto now   = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

void Logger::emit(Level level, const char* color, const char* tag, const std::string& message) {
    if (level < m_minLevel) return;
    std::lock_guard<std::mutex> lock(m_logMutex);
    const std::string ts   = "[" + timestamp() + "] ";
    const std::string line = ts + tag + " " + message + '\n';
    // Colored console (errors → stderr, rest → stdout)
    if (level == Level::ERROR) {
        std::cerr << ts << color << tag << "\033[0m " << message << '\n';
    } else {
        std::cout << ts << color << tag << "\033[0m " << message << '\n';
    }
    // Plain file (no ANSI codes)
    if (m_logFile.is_open()) {
        m_logFile << line;
        m_logFile.flush();
    }
}

void Logger::error(const std::string& message) {
    emit(Level::ERROR,   "\033[1;31m", "[ERROR]  ", message);
}

void Logger::warning(const std::string& message) {
    emit(Level::WARNING, "\033[1;33m", "[WARN]   ", message);
}

void Logger::info(const std::string& message) {
    emit(Level::INFO,    "\033[1;32m", "[INFO]   ", message);
}

void Logger::debug(const std::string& message) {
    emit(Level::DEBUG,   "\033[0;37m", "[DEBUG]  ", message);
}

void Logger::json(const std::string& message) {
    emit(Level::DEBUG,   "\033[1;36m", "[JSON]   ", message);
}
