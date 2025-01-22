#ifndef TELEGRAMBOT_HPP
#define TELEGRAMBOT_HPP

#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <chrono>
#include <thread>
#include <mutex>

#include <curl/curl.h>
#include <json/json.h>
#include "logger.hpp"
#include "network.hpp"

/**
 * @class RateLimiter
 * @brief A rate limiter to control the number of requests a user can make within a specified time interval.
 */
class RateLimiter {
public:
    /**
     * @brief Constructs a RateLimiter object.
     * @param maxRequests The maximum number of requests allowed within the interval.
     * @param interval The time interval (in seconds) during which the requests are counted.
     */
    RateLimiter(int maxRequests, std::chrono::seconds interval);

    /**
     * @brief Checks if a request from a user is allowed based on the rate limit.
     * @param userId The unique identifier of the user making the request.
     * @return True if the request is allowed, false if the rate limit is exceeded.
     */
    bool allowRequest(const std::string& userId);

private:
    int maxRequests; ///< Maximum number of requests allowed within the interval.
    std::chrono::seconds interval; ///< Time interval for rate limiting.
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> userRequestTimestamps; ///< Timestamps of requests per user.
};

// Forward declaration of Command struct
struct Command;

/**
 * @class TelegramBot
 * @brief A class representing a Telegram bot that can handle commands and send messages.
 */
class TelegramBot {
public:
    /**
     * @brief Constructs a TelegramBot object.
     * @param token The API token for the Telegram bot.
     */
    TelegramBot(const std::string& token);

    /**
     * @brief Registers a command with the bot.
     * @param command The command to register.
     */
    void registerCommand(const Command& command);

    /**
     * @brief Starts the bot, polling for updates in a loop.
     */
    void start();

    /**
     * @brief Sends a message to a specific chat.
     * @param chatId The ID of the chat to send the message to.
     * @param text The text of the message to send.
     */
    void sendMessage(const std::string& chatId, const std::string& text, bool useMarkdown = false);

private:
    std::string token; ///< The Telegram bot API token.
    std::string apiUrl; ///< The base URL for the Telegram API.
    long long lastUpdateId; ///< The ID of the last processed update.
    std::unordered_map<std::string, Command> commandHandlers; ///< Map of registered commands and their handlers.
    std::mutex commandMutex; ///< Mutex to protect access to commandHandlers.
    std::mutex updateMutex; ///< Mutex to protect access to lastUpdateId.
    RateLimiter rateLimiter; ///< Rate limiter to control user requests.
    Network network; ///< Network utility for making HTTP requests.

    /**
     * @brief URL-encodes a string.
     * @param text The string to encode.
     * @return The URL-encoded string.
     */
    std::string urlEncode(const std::string& text);

    /**
     * @brief Parses a JSON response from the Telegram API.
     * @param jsonResponse The JSON response as a string.
     * @return An optional Json::Value containing the parsed JSON, or std::nullopt if parsing fails.
     */
    std::optional<Json::Value> parseJsonResponse(const std::string& jsonResponse);

    /**
     * @brief Polls the Telegram API for new updates.
     */
    void pollUpdates();

    /**
     * @brief Processes updates received from the Telegram API.
     * @param updates The JSON object containing the updates.
     */
    void processUpdates(const Json::Value& updates);

    /**
     * @brief Gets the ID of the last processed update.
     * @return The last update ID.
     */
    long long getLastUpdateId();

    /**
     * @brief Updates the ID of the last processed update.
     * @param updateId The new update ID.
     */
    void updateLastUpdateId(long long updateId);
};

/**
 * @class Command
 * @brief A class representing a command that the bot can handle.
 */
class Command {
public:
    /**
     * @brief Default constructor for Command.
     */
    Command();

    /**
     * @brief Constructs a Command object.
     * @param name The name of the command.
     * @param handler The function to handle the command.
     */
    Command(const std::string& name, std::function<void(const std::string&, const std::string&, TelegramBot&)> handler);

    /**
     * @brief Gets the name of the command.
     * @return The name of the command.
     */
    std::string getName() const;

    /**
     * @brief Executes the command.
     * @param chatId The ID of the chat where the command was issued.
     * @param args The arguments provided with the command.
     * @param bot The TelegramBot instance to interact with.
     */
    void execute(const std::string& chatId, const std::string& args, TelegramBot& bot) const;

private:
    std::string name; ///< The name of the command.
    std::function<void(const std::string&, const std::string&, TelegramBot&)> handler; ///< The function to handle the command.
};

#endif // TELEGRAMBOT_HPP
