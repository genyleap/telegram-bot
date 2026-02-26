#ifndef STATE_MANAGER_HPP
#define STATE_MANAGER_HPP

#include <string>
#include <fstream>
#include "logger.hpp"

/**
 * @class StateManager
 * @brief Persists bot state (e.g. lastUpdateId) to disk so restarts don't
 *        re-process or lose pending Telegram updates.
 *
 * State is stored as a simple text file containing a single integer.
 */
class StateManager {
public:
    explicit StateManager(const std::string& filePath = ".bot_state")
        : m_filePath(filePath) {}

    /**
     * @brief Reads lastUpdateId from disk. Returns 0 if the file doesn't exist.
     */
    long long loadLastUpdateId() const {
        std::ifstream ifs(m_filePath);
        if (!ifs.is_open()) return 0LL;
        long long id = 0;
        ifs >> id;
        Logger::formattedInfo("StateManager: loaded lastUpdateId = {}", id);
        return id;
    }

    /**
     * @brief Writes lastUpdateId to disk atomically (write-then-rename).
     */
    void saveLastUpdateId(long long id) const {
        const std::string tmp = m_filePath + ".tmp";
        {
            std::ofstream ofs(tmp);
            if (!ofs.is_open()) {
                Logger::error("StateManager: cannot write state file: " + tmp);
                return;
            }
            ofs << id << '\n';
        }
        // Atomic replace
        if (std::rename(tmp.c_str(), m_filePath.c_str()) != 0) {
            Logger::error("StateManager: rename failed for state file.");
        }
    }

private:
    std::string m_filePath;
};

#endif // STATE_MANAGER_HPP
