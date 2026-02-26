#include "persistence.hpp"
#include "logger.hpp"
#include <fstream>
#include <cstdio>
#include <chrono>
#include <filesystem>

Persistence::Persistence(const std::string& filePath) : m_filePath(filePath) {
    load();
}

void Persistence::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream ifs(m_filePath);
    if (!ifs.is_open()) {
        m_root = Json::Value(Json::objectValue);
        m_root["reminders"] = Json::Value(Json::arrayValue);
        m_root["custom_commands"] = Json::Value(Json::arrayValue);
        m_root["filtered_keywords"] = Json::Value(Json::arrayValue);
        m_root["welcome_message"] = "";
        return;
    }

    Json::CharReaderBuilder reader;
    std::string errs;
    if (!Json::parseFromStream(reader, ifs, &m_root, &errs)) {
        Logger::error("Persistence: failed to parse " + m_filePath + ": " + errs);
        m_root = Json::Value(Json::objectValue);
        m_root["reminders"] = Json::Value(Json::arrayValue);
        m_root["custom_commands"] = Json::Value(Json::arrayValue);
        m_root["filtered_keywords"] = Json::Value(Json::arrayValue);
        m_root["welcome_message"] = "";
    }
}

void Persistence::save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string tmp = m_filePath + ".tmp";
    
    // Create directory if it doesn't exist
    std::filesystem::path p(m_filePath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    {
        std::ofstream ofs(tmp);
        if (!ofs.is_open()) {
            Logger::error("Persistence: failed to write to " + tmp);
            return;
        }
        Json::StreamWriterBuilder writer;
        ofs << Json::writeString(writer, m_root);
    }

    std::rename(tmp.c_str(), m_filePath.c_str());
}

void Persistence::addReminder(const Reminder& reminder) {
    std::lock_guard<std::mutex> lock(m_mutex);
    Json::Value jRem;
    jRem["chat_id"]      = reminder.chatId;
    jRem["message"]      = reminder.message;
    jRem["trigger_time"] = static_cast<Json::Int64>(reminder.triggerTime);
    m_root["reminders"].append(jRem);
    
    // We unlock before save to avoid deadlock if save ever used recursion (it doesn't here but good practice)
}

std::vector<Reminder> Persistence::getPendingReminders() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Reminder> result;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    for (const auto& jRem : m_root["reminders"]) {
        if (jRem["trigger_time"].asInt64() <= now) {
            result.push_back({
                jRem["chat_id"].asString(),
                jRem["message"].asString(),
                jRem["trigger_time"].asInt64()
            });
        }
    }
    return result;
}

void Persistence::removeReminder(size_t index) {
    // Note: This is an inefficient implementation for large arrays, but fine for simple bots.
    std::lock_guard<std::mutex> lock(m_mutex);
    Json::Value newReminders(Json::arrayValue);
    for (unsigned int i = 0; i < m_root["reminders"].size(); ++i) {
        if (i != index) {
            newReminders.append(m_root["reminders"][i]);
        }
    }
    m_root["reminders"] = newReminders;
}

void Persistence::addCustomCommand(const CustomCommand& cmd) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_root.isMember("custom_commands")) {
        m_root["custom_commands"] = Json::Value(Json::arrayValue);
    }
    
    // Check if command already exists and update it
    bool updated = false;
    for (auto& jCmd : m_root["custom_commands"]) {
        if (jCmd["name"].asString() == cmd.name) {
            jCmd["type"] = cmd.type;
            jCmd["content"] = cmd.content;
            updated = true;
            break;
        }
    }

    if (!updated) {
        Json::Value jCmd;
        jCmd["name"] = cmd.name;
        jCmd["type"] = cmd.type;
        jCmd["content"] = cmd.content;
        m_root["custom_commands"].append(jCmd);
    }
}

std::vector<CustomCommand> Persistence::getCustomCommands() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<CustomCommand> result;
    if (m_root.isMember("custom_commands")) {
        for (const auto& jCmd : m_root["custom_commands"]) {
            result.push_back({
                jCmd["name"].asString(),
                jCmd["type"].asString(),
                jCmd["content"].asString()
            });
        }
    }
    return result;
}

void Persistence::setWelcomeMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_root["welcome_message"] = message;
}

std::string Persistence::getWelcomeMessage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_root.get("welcome_message", "").asString();
}

void Persistence::addFilteredKeyword(const std::string& keyword) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_root.isMember("filtered_keywords")) {
        m_root["filtered_keywords"] = Json::Value(Json::arrayValue);
    }
    for (const auto& k : m_root["filtered_keywords"]) {
        if (k.asString() == keyword) return;
    }
    m_root["filtered_keywords"].append(keyword);
}

void Persistence::removeFilteredKeyword(const std::string& keyword) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_root.isMember("filtered_keywords")) return;
    Json::Value newKeywords(Json::arrayValue);
    for (const auto& k : m_root["filtered_keywords"]) {
        if (k.asString() != keyword) {
            newKeywords.append(k);
        }
    }
    m_root["filtered_keywords"] = newKeywords;
}

std::vector<std::string> Persistence::getFilteredKeywords() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result;
    if (m_root.isMember("filtered_keywords")) {
        for (const auto& k : m_root["filtered_keywords"]) {
            result.push_back(k.asString());
        }
    }
    return result;
}
