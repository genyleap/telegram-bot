#ifndef PERSISTENCE_HPP
#define PERSISTENCE_HPP

#include <string>
#include <vector>
#include <mutex>
#include <json/json.h>

/**
 * @struct Reminder
 * @brief Represents a scheduled notification for a user.
 */
struct Reminder {
    std::string chatId;
    std::string message;
    long long   triggerTime; // Unix timestamp
};

struct CustomCommand {
    std::string name;
    std::string type; // "static" or "api"
    std::string content;
};

struct RSSFeed {
    std::string url;
    std::string title;
    std::string description;
    std::string emoji;
    std::string logoUrl;
    std::string chatId;
    std::string lastGuid;
};

/**
 * @class Persistence
 * @brief Handles persistent storage of bot data (reminders, settings) using JSON.
 */
class Persistence {
public:
    explicit Persistence(const std::string& filePath = "data/bot_data.json");

    // Reminders
    void addReminder(const Reminder& reminder);
    std::vector<Reminder> getPendingReminders();
    void removeReminder(size_t index);

    // Custom Commands
    void addCustomCommand(const CustomCommand& cmd);
    std::vector<CustomCommand> getCustomCommands();

    // Group Management
    void setWelcomeMessage(const std::string& message);
    std::string getWelcomeMessage();
    void addFilteredKeyword(const std::string& keyword);
    void removeFilteredKeyword(const std::string& keyword);
    std::vector<std::string> getFilteredKeywords();

    // RSS feeds
    void addRSSFeed(const RSSFeed& feed);
    void removeRSSFeed(const std::string& url);
    std::vector<RSSFeed> getRSSFeeds();
    void updateRSSFeed(const RSSFeed& feed);

    void save();

private:
    std::string m_filePath;
    Json::Value m_root;
    mutable std::mutex m_mutex;

    void load();
};

#endif // PERSISTENCE_HPP
