#ifndef TELEGRAMBOT_HPP
#define TELEGRAMBOT_HPP

#include <string>
#include <unordered_map>
#include <functional>
#include <optional>
#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <deque>
#include <csignal>

#include <curl/curl.h>
#include <json/json.h>
#include "logger.hpp"
#include "network.hpp"
#include "threadpool.hpp"
#include "statemanager.hpp"
#include "persistence.hpp"
#include "webhookserver.hpp"

// ─── Forward declarations ─────────────────────────────────────────────────────
class TelegramBot;
class Command;
class AIBrain;
class RSSManager;
struct AIResponse;

/**
 * @struct BotConfig
 * @brief Runtime configuration for TelegramBot.
 */
struct BotConfig {
    bool        debug           = false;
    std::string token;                    ///< Optional: can be set in JSON config
    int         threadPoolSize  = 4;
    std::string logFile;
    std::string stateFile       = ".bot_state";
    int         longPollTimeout = 30;     ///< getUpdates timeout in seconds (polling mode)
    int         rssCheckInterval = 900;    ///< Interval between RSS checks in seconds
    std::string aiApiKey;

    // ── Webhook mode ──────────────────────────────────────────────────────
    bool        webhookMode     = false;
    std::string webhookUrl;               ///< Public HTTPS URL Telegram sends updates to
    std::string webhookPath     = "/webhook"; ///< Path on this server
    int         webhookPort     = 8443;   ///< Local TCP port (behind TLS proxy)
};

// ─── Keyboard & Media Models ──────────────────────────────────────────────────

/**
 * @struct InlineKeyboardButton
 */
struct InlineKeyboardButton {
    std::string text;
    std::string callbackData; ///< Data sent back in callback_query
    std::string url;          ///< Optional URL to open
};

/**
 * @struct InlineKeyboardMarkup
 */
struct InlineKeyboardMarkup {
    std::vector<std::vector<InlineKeyboardButton>> inlineKeyboard;
    Json::Value toJson() const;
};

/**
 * @struct ReplyKeyboardButton
 */
struct ReplyKeyboardButton {
    std::string text;
};

/**
 * @struct ReplyKeyboardMarkup
 */
struct ReplyKeyboardMarkup {
    std::vector<std::vector<ReplyKeyboardButton>> keyboard;
    bool resizeKeyboard = true;
    bool oneTimeKeyboard = false;
    Json::Value toJson() const;
};

/**
 * @struct ReplyKeyboardRemove
 */
struct ReplyKeyboardRemove {
    bool removeKeyboard = true;
    Json::Value toJson() const;
};

/**
 * @class RateLimiter
 * @brief Per-user inbound rate limiter (sliding-window token bucket).
 */
class RateLimiter {
public:
    RateLimiter(int maxRequests, std::chrono::seconds interval);
    bool allowRequest(const std::string& userId);

private:
    int                   m_maxRequests;
    std::chrono::seconds  m_interval;
    std::map<std::string, std::vector<std::chrono::steady_clock::time_point>> m_userRequestTimestamps;
    std::mutex            m_rateLimiterMutex;
};

/**
 * @class OutboundRateLimiter
 * @brief Enforces Telegram API outbound limits:
 *   • 30 messages / second  (global)
 *   • 20 messages / minute  (per chat)
 * Callers block until it is safe to send.
 */
class OutboundRateLimiter {
public:
    OutboundRateLimiter() = default;
    void throttle(const std::string& chatId, volatile std::sig_atomic_t* shutdownPtr = nullptr);

private:
    std::mutex                                                          m_mutex;
    std::deque<std::chrono::steady_clock::time_point>                  m_globalWindow;
    std::map<std::string, std::deque<std::chrono::steady_clock::time_point>> m_perChatWindow;

    static constexpr int kGlobalMax  = 30;  ///< msgs / second
    static constexpr int kPerChatMax = 20;  ///< msgs / minute
};

/**
 * @class TelegramBot
 * @brief Production-grade Telegram bot with long-polling, async dispatch,
 *        outbound rate limiting, persistent state, and graceful shutdown.
 */
class TelegramBot {
public:
    explicit TelegramBot(const std::string& token, const BotConfig& config = {});
    ~TelegramBot();

    void registerCommand(const Command& command);

    /**
     * @brief Runs the polling loop until @p shutdown becomes non-zero.
     */
    void start(volatile std::sig_atomic_t& shutdown);

    /**
     * @brief Registers a webhook URL with the Telegram API.
     * @param url Full public HTTPS URL Telegram should POST updates to.
     * @return true on success.
     */
    bool setWebhook(const std::string& url);

    /**
     * @brief Removes the registered webhook (reverts to long-polling mode).
     * @return true on success.
     */
    bool deleteWebhook();

    /**
     * @brief Starts a webhook HTTP server and blocks until @p shutdown fires.
     */
    void startWebhook(volatile std::sig_atomic_t& shutdown);

    /**
     * @brief Sends a text message with optional keyboard.
     */
    bool sendMessage(const std::string& chatId, const std::string& text,
                     bool useMarkdown = false,
                     std::optional<Json::Value> replyMarkup = std::nullopt);

    /**
     * @brief Answers a callback query (from an inline button click).
     */
    bool answerCallbackQuery(const std::string& callbackQueryId,
                             const std::string& text = "",
                             bool showAlert = false);

    /**
     * @brief Sends a photo.
     * @param photo File path, URL, or file_id.
     */
    bool sendPhoto(const std::string& chatId, const std::string& photo,
                   const std::string& caption = "");

    /**
     * @brief Sends a document / file.
     */
    bool sendDocument(const std::string& chatId, const std::string& document,
                      const std::string& caption = "");

    // ── Group Management ──────────────────────────────────────────────────

    bool banChatMember(const std::string& chatId, long long userId);
    bool unbanChatMember(const std::string& chatId, long long userId);
    bool promoteChatMember(const std::string& chatId, long long userId, bool canChangeInfo = false);
    bool kickChatMember(const std::string& chatId, long long userId); // Deprecated alias for ban

    /**
     * @brief Sends an auto-generated /help listing all registered commands.
     */
    void sendHelpMessage(const std::string& chatId);

    /**
     * @brief Schedules a reminder.
     */
    void addReminder(const Reminder& reminder);

    /**
     * @brief Registers a user-defined custom command.
     */
    void registerCustomCommand(const CustomCommand& cc);

    // Group Management Wrappers
    void setWelcomeMessage(const std::string& message) { m_persistence.setWelcomeMessage(message); m_persistence.save(); }
    void addFilteredKeyword(const std::string& keyword) { m_persistence.addFilteredKeyword(keyword); m_persistence.save(); }
    void removeFilteredKeyword(const std::string& keyword) { m_persistence.removeFilteredKeyword(keyword); m_persistence.save(); }
    std::vector<std::string> getFilteredKeywords() { return m_persistence.getFilteredKeywords(); }

    // RSS Support
    void addRSSFeed(const std::string& chatId, const std::string& url);
    void removeRSSFeed(const std::string& chatId, const std::string& url);
    void updateRSSFeed(const RSSFeed& feed);
    std::vector<RSSFeed> getRSSFeeds(const std::string& chatId);
    void checkRSSFeeds();

private:
    std::string  m_token;
    std::string  m_apiUrl;
    long long    m_lastUpdateId = 0;
    BotConfig    m_config;

    std::unordered_map<std::string, Command> m_commandHandlers;
    std::mutex   m_commandMutex;
    std::mutex   m_updateMutex;

    RateLimiter         m_rateLimiter;
    OutboundRateLimiter m_outboundLimiter;
    Network             m_sendNetwork;
    Network             m_pollNetwork;
    ThreadPool          m_threadPool;
    StateManager        m_stateManager;
    Persistence         m_persistence;
    std::unique_ptr<AIBrain> m_aiBrain;
    std::unique_ptr<RSSManager> m_rssManager;

    volatile std::sig_atomic_t* m_shutdownPtr = nullptr;

    // Exponential back-off state
    int  m_backoffSeconds = 1;
    static constexpr int kMaxBackoff = 60;

    std::optional<Json::Value> parseJsonResponse(const std::string& jsonResponse);
    void      pollUpdates();
    void      processUpdates(const Json::Value& updates);
    long long getLastUpdateId();
    void      updateLastUpdateId(long long updateId);
    static std::string escapeMarkdown(const std::string& text);
    static std::string stripHTML(const std::string& text);

public:
    Network& getNetwork() { return m_sendNetwork; }
    AIResponse askAI(long long userId, const std::string& query);
};

/**
 * @class Command
 * @brief Handler + metadata for a single bot command.
 */
class Command {
public:
    Command();
    Command(const std::string& name,
            const std::string& description,
            std::function<void(const std::string&, const std::string&, TelegramBot&)> handler);
    // Backwards-compatible constructor (no description)
    Command(const std::string& name,
            std::function<void(const std::string&, const std::string&, TelegramBot&)> handler);

    std::string getName()        const;
    std::string getDescription() const;
    void execute(const std::string& chatId, const std::string& args, TelegramBot& bot) const;

private:
    std::string m_name;
    std::string m_description;
    std::function<void(const std::string&, const std::string&, TelegramBot&)> m_handler;
};

#endif // TELEGRAMBOT_HPP
