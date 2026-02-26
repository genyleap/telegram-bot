#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <mutex>
#include <format>
#include <fstream>
#include <iostream>

/**
 * @class Logger
 * @brief Thread-safe logging with timestamps, severity levels, and optional file output.
 *
 * Log levels (ascending): DEBUG < INFO < WARNING < ERROR
 * Only messages at or above the configured minimum level are emitted.
 */
class Logger {
public:
    enum class Level { DEBUG = 0, INFO = 1, WARNING = 2, ERROR = 3 };

    /**
     * @brief Configure file output and minimum log level.
     * @param filepath Path to the log file (empty = no file output).
     * @param minLevel Minimum severity to emit.
     */
    static void configure(const std::string& filepath = "", Level minLevel = Level::INFO);

    static void error(const std::string& message);
    static void warning(const std::string& message);
    static void info(const std::string& message);
    static void debug(const std::string& message);

    /// Logs a raw JSON payload (cyan). Always emits at DEBUG level.
    static void json(const std::string& message);

    template<typename... Args>
    static void formattedError(std::format_string<Args...> fmt, Args&&... args) {
        error(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void formattedWarning(std::format_string<Args...> fmt, Args&&... args) {
        warning(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void formattedInfo(std::format_string<Args...> fmt, Args&&... args) {
        info(std::format(fmt, std::forward<Args>(args)...));
    }

    template<typename... Args>
    static void formattedDebug(std::format_string<Args...> fmt, Args&&... args) {
        debug(std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static std::mutex   m_logMutex;
    static std::ofstream m_logFile;
    static Level        m_minLevel;

    static std::string timestamp();
    static void        emit(Level level, const char* color, const char* tag, const std::string& message);
};

#endif // LOGGER_HPP
