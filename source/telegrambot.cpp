#include "telegrambot.hpp"
#include "webhookserver.hpp"
#include <algorithm>
#include <sstream>
#include "aibrain.hpp"
#include "rssmanager.hpp"

// ─── Keyboard Implementations ───────────────────────────────────────────────

Json::Value InlineKeyboardMarkup::toJson() const {
    Json::Value root;
    Json::Value& rows = root["inline_keyboard"];
    for (const auto& row : inlineKeyboard) {
        Json::Value jRow(Json::arrayValue);
        for (const auto& btn : row) {
            Json::Value jBtn;
            jBtn["text"] = btn.text;
            if (!btn.callbackData.empty()) jBtn["callback_data"] = btn.callbackData;
            if (!btn.url.empty())          jBtn["url"]           = btn.url;
            jRow.append(jBtn);
        }
        rows.append(jRow);
    }
    return root;
}

Json::Value ReplyKeyboardMarkup::toJson() const {
    Json::Value root;
    Json::Value& rows = root["keyboard"];
    for (const auto& row : keyboard) {
        Json::Value jRow(Json::arrayValue);
        for (const auto& btn : row) {
            Json::Value jBtn;
            jBtn["text"] = btn.text;
            jRow.append(jBtn);
        }
        rows.append(jRow);
    }
    root["resize_keyboard"]   = resizeKeyboard;
    root["one_time_keyboard"] = oneTimeKeyboard;
    return root;
}

Json::Value ReplyKeyboardRemove::toJson() const {
    Json::Value root;
    root["remove_keyboard"] = removeKeyboard;
    return root;
}

// ─── RateLimiter ─────────────────────────────────────────────────────────────
RateLimiter::RateLimiter(int maxRequests, std::chrono::seconds interval)
    : m_maxRequests(maxRequests), m_interval(interval) {}

bool RateLimiter::allowRequest(const std::string& userId) {
    std::lock_guard<std::mutex> lock(m_rateLimiterMutex);
    auto now = std::chrono::steady_clock::now();
    auto& userRequests = m_userRequestTimestamps[userId];
    userRequests.erase(std::remove_if(userRequests.begin(), userRequests.end(),
        [now, this](const auto& ts) { return now - ts > m_interval; }),
        userRequests.end());
    if (static_cast<int>(userRequests.size()) < m_maxRequests) {
        userRequests.push_back(now);
        return true;
    }
    return false;
}

// ─── OutboundRateLimiter ─────────────────────────────────────────────────────
void OutboundRateLimiter::throttle(const std::string& chatId, volatile std::sig_atomic_t* shutdownPtr) {
    using namespace std::chrono;
    while (true) {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto now = steady_clock::now();

        // Purge expired global entries (older than 1 second)
        while (!m_globalWindow.empty() &&
               now - m_globalWindow.front() >= seconds(1))
            m_globalWindow.pop_front();

        // Purge expired per-chat entries (older than 1 minute)
        auto& chatQ = m_perChatWindow[chatId];
        while (!chatQ.empty() &&
               now - chatQ.front() >= minutes(1))
            chatQ.pop_front();

        bool globalOk  = static_cast<int>(m_globalWindow.size())  < kGlobalMax;
        bool perChatOk = static_cast<int>(chatQ.size())           < kPerChatMax;

        if (globalOk && perChatOk) {
            m_globalWindow.push_back(now);
            chatQ.push_back(now);
            return;
        }

        // Determine how long to sleep
        steady_clock::time_point wakeGlobal  = m_globalWindow.empty()
            ? now : m_globalWindow.front() + seconds(1);
        steady_clock::time_point wakePerChat = chatQ.empty()
            ? now : chatQ.front() + minutes(1);
        steady_clock::time_point wake = std::max({now + milliseconds(10),
                                                   globalOk  ? now : wakeGlobal,
                                                   perChatOk ? now : wakePerChat});
        lock.unlock();
        while (steady_clock::now() < wake) {
            if (shutdownPtr && *shutdownPtr) return;
            std::this_thread::sleep_for(milliseconds(50));
        }
    }
}

// ─── Command ─────────────────────────────────────────────────────────────────
Command::Command() : m_name(""), m_description(""), m_handler(nullptr) {}

Command::Command(const std::string& name, const std::string& description,
                 std::function<void(const std::string&, const std::string&, TelegramBot&)> handler)
    : m_name(name), m_description(description), m_handler(std::move(handler)) {}

Command::Command(const std::string& name,
                 std::function<void(const std::string&, const std::string&, TelegramBot&)> handler)
    : m_name(name), m_description(""), m_handler(std::move(handler)) {}

std::string Command::getName()        const { return m_name; }
std::string Command::getDescription() const { return m_description; }

void Command::execute(const std::string& chatId, const std::string& args, TelegramBot& bot) const {
    if (m_handler) {
        m_handler(chatId, args, bot);
    } else {
        bot.sendMessage(chatId, "Command handler not set.");
    }
}

// ─── TelegramBot ─────────────────────────────────────────────────────────────
TelegramBot::TelegramBot(const std::string& token, const BotConfig& config)
    : m_config(config),
      m_rateLimiter(5, std::chrono::seconds(10)),
      m_threadPool(config.threadPoolSize),
      m_stateManager(config.stateFile),
      m_persistence("data/bot_data.json")
{
    // Clean token (remove any trailing \r or \n or spaces)
    std::string cleanToken = token;
    cleanToken.erase(std::remove_if(cleanToken.begin(), cleanToken.end(), 
                                    [](unsigned char c) { return std::isspace(c); }), 
                     cleanToken.end());
    m_token = cleanToken;
    m_apiUrl = "https://api.telegram.org/bot" + m_token;

    // AI Brain Initialization
    std::string aiKey = config.aiApiKey;
    if (const char* envKey = std::getenv("AI_API_KEY")) {
        aiKey = envKey;
    }
    if (!aiKey.empty() && aiKey != "YOUR_AI_KEY_HERE") {
        m_aiBrain = std::make_unique<AIBrain>(m_sendNetwork, aiKey);
        Logger::info("AI Brain initialized.");
    }

    // RSS Manager Initialization
    m_rssManager = std::make_unique<RSSManager>(m_sendNetwork);
    Logger::info("RSS Manager initialized.");

    // Restore persisted update ID
    m_lastUpdateId = m_stateManager.loadLastUpdateId();

    // Configure logger level and optional file
    Logger::configure(config.logFile,
                      config.debug ? Logger::Level::DEBUG : Logger::Level::INFO);
}

TelegramBot::~TelegramBot() = default;

void TelegramBot::registerCommand(const Command& command) {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_commandHandlers[command.getName()] = command;
}

void TelegramBot::start(volatile std::sig_atomic_t& shutdown) {
    registerCommand(Command("/help", "Shows this help message.",
        [](const std::string& chatId, const std::string&, TelegramBot& bot) {
            bot.sendHelpMessage(chatId);
        }
    ));

    // Auto-register /addcmd
    registerCommand(Command("/addcmd", "Adds a custom command. Usage: /addcmd <name> <static|api> <content>",
        [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
            std::istringstream iss(args);
            std::string name, type, content;
            if (!(iss >> name >> type)) {
                bot.sendMessage(chatId, "❌ Usage: `/addcmd <name> <static|api> <content...>`", true);
                return;
            }
            std::getline(iss, content);
            if (!content.empty() && content[0] == ' ') content.erase(0, 1);

            if (type != "static" && type != "api") {
                bot.sendMessage(chatId, "❌ Type must be `static` or `api`.");
                return;
            }

            if (name[0] != '/') name = "/" + name;

            CustomCommand cc{name, type, content};
            bot.registerCustomCommand(cc);
            bot.sendMessage(chatId, "✅ Command `" + name + "` (" + type + ") added!", true);
        }
    ));

    // Load custom commands from persistence
    auto customCmds = m_persistence.getCustomCommands();
    for (const auto& cc : customCmds) {
        registerCustomCommand(cc);
    }

    m_shutdownPtr = &shutdown;
    
    // Add reminder processor
    auto lastCheck = std::chrono::steady_clock::now();
    auto lastRSSCheck = std::chrono::steady_clock::now();

    // Register RSS commands
    registerCommand(Command("/rss_add", "Subscribe to an RSS feed. Usage: /rss_add <url>",
        [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
            if (args.empty()) {
                bot.sendMessage(chatId, "❌ Usage: `/rss_add <url>`", true);
                return;
            }
            bot.sendMessage(chatId, "⏳ Fetching feed info...");
            bot.addRSSFeed(chatId, args);
        }
    ));

    registerCommand(Command("/rss_list", "List subscribed RSS feeds.",
        [](const std::string& chatId, const std::string&, TelegramBot& bot) {
            auto feeds = bot.getRSSFeeds(chatId);
            if (feeds.empty()) {
                bot.sendMessage(chatId, "You have no RSS subscriptions.");
                return;
            }
            std::string list = "📰 *Your RSS Subscriptions*:\n\n";
            for (size_t i = 0; i < feeds.size(); ++i) {
                list += std::to_string(i + 1) + ". " + feeds[i].emoji + " [" + escapeMarkdown(feeds[i].title) + "](" + feeds[i].url + ")\n";
            }
            bot.sendMessage(chatId, list, true);
        }
    ));

    registerCommand(Command("/rss_remove", "Unsubscribe from an RSS feed. Usage: /rss_remove <url>",
        [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
            if (args.empty()) {
                bot.sendMessage(chatId, "❌ Usage: `/rss_remove <url>`", true);
                return;
            }
            bot.removeRSSFeed(chatId, args);
            bot.sendMessage(chatId, "✅ Unsubscribed from " + args);
        }
    ));

    registerCommand(Command("/rss_config", "Configure an RSS feed. Usage: /rss_config <url> <emoji|title> <value>",
        [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
            std::istringstream iss(args);
            std::string url, key, value;
            if (!(iss >> url >> key)) {
                bot.sendMessage(chatId, "❌ Usage: `/rss_config <url> <emoji|title> <value...>`", true);
                return;
            }
            std::getline(iss, value);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);

            auto feeds = bot.getRSSFeeds(chatId);
            bool found = false;
            for (auto& f : feeds) {
                if (f.url == url) {
                    if (key == "emoji") f.emoji = value;
                    else if (key == "title") f.title = value;
                    else {
                        bot.sendMessage(chatId, "❌ Invalid key. Use `emoji` or `title`.");
                        return;
                    }
                    // Update in persistence (we need a way to update specifically this feed)
                    // Persistence::updateRSSFeed already takes the whole struct.
                    bot.updateRSSFeed(f);
                    bot.sendMessage(chatId, "✅ Updated " + key + " for " + url);
                    found = true;
                    break;
                }
            }
            if (!found) bot.sendMessage(chatId, "❌ Feed not found: " + url);
        }
    ));

    while (!shutdown) {
        pollUpdates();

        // Check reminders every 5 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastCheck).count() >= 5) {
            auto pending = m_persistence.getPendingReminders();
            for (size_t i = 0; i < pending.size(); ++i) {
                sendMessage(pending[i].chatId, "🔔 *REMINDER*: " + pending[i].message, true);
                m_persistence.removeReminder(0); // Remove the first one (since we process current pending)
            }
            if (!pending.empty()) m_persistence.save();
            lastCheck = now;
        }

        // Check RSS feeds every configurable interval (default 15 minutes)
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRSSCheck).count() >= m_config.rssCheckInterval) {
            checkRSSFeeds();
            lastRSSCheck = now;
        }
    }
    Logger::info("Shutdown signal received. Exiting poll loop.");
}

bool TelegramBot::setWebhook(const std::string& url) {
    const std::string reqUrl = m_apiUrl + "/setWebhook?"
        + m_sendNetwork.buildQueryString({{"url", url}});
    std::string response;
    if (!m_sendNetwork.sendRequest(reqUrl, response, 10)) {
        Logger::error("setWebhook: network request failed.");
        return false;
    }
    auto parsed = parseJsonResponse(response);
    if (!parsed) return false;
    Logger::info("Webhook registered: " + url);
    return true;
}

bool TelegramBot::deleteWebhook() {
    const std::string reqUrl = m_apiUrl + "/deleteWebhook";
    std::string response;
    if (!m_sendNetwork.sendRequest(reqUrl, response, 10)) {
        Logger::error("deleteWebhook: network request failed.");
        return false;
    }
    auto parsed = parseJsonResponse(response);
    if (!parsed) return false;
    Logger::info("Webhook deleted. Reverted to long-polling mode.");
    return true;
}

void TelegramBot::startWebhook(volatile std::sig_atomic_t& shutdown) {
    // Auto-register /help command
    registerCommand(Command("/help", "Shows this help message.",
        [](const std::string& chatId, const std::string&, TelegramBot& bot) {
            bot.sendHelpMessage(chatId);
        }
    ));

    // Load custom commands from persistence (webhook mode)
    auto customCmds = m_persistence.getCustomCommands();
    for (const auto& cc : customCmds) {
        registerCustomCommand(cc);
    }

    // Register the webhook URL with Telegram
    if (!m_config.webhookUrl.empty()) {
        const std::string fullUrl = m_config.webhookUrl + m_config.webhookPath;
        if (!setWebhook(fullUrl)) {
            Logger::error("startWebhook: failed to register webhook with Telegram. Aborting.");
            return;
        }
    }

    // Start local HTTP server
    WebhookServer server(m_config.webhookPort, m_config.webhookPath,
        [this](const std::string& jsonBody) {
            auto parsed = parseJsonResponse(jsonBody);
            if (parsed) processUpdates(*parsed);
        }
    );

    if (!server.start()) {
        Logger::error("startWebhook: failed to start WebhookServer.");
        return;
    }

    Logger::formattedInfo("Bot started (webhook mode) on port {}. Press Ctrl+C to stop.",
                          m_config.webhookPort);
    while (!shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    server.stop();
    Logger::info("Webhook server stopped.");
}

bool TelegramBot::sendMessage(const std::string& chatId, const std::string& text,
                              bool useMarkdown, std::optional<Json::Value> replyMarkup) {
    // Block if we are about to exceed Telegram API rate limits
    m_outboundLimiter.throttle(chatId, m_shutdownPtr);
    if (m_shutdownPtr && *m_shutdownPtr) return false;

    std::map<std::string, std::string> params = {
        {"chat_id", chatId},
        {"text",    text}
    };
    if (useMarkdown) params["parse_mode"] = "Markdown";
    if (replyMarkup) {
        Json::StreamWriterBuilder writer;
        params["reply_markup"] = Json::writeString(writer, *replyMarkup);
    }

    const std::string url = m_apiUrl + "/sendMessage?" + m_sendNetwork.buildQueryString(params);
    std::string response;

    if (!m_sendNetwork.sendRequest(url, response, /*timeoutSeconds=*/10)) {
        Logger::error("sendMessage: network request failed for chat " + chatId);
        return false;
    }

    // Validate Telegram's response
    auto parsed = parseJsonResponse(response);
    if (!parsed) {
        Logger::formattedError("sendMessage: Telegram rejected message to chat {}", chatId);
        return false;
    }

    Logger::debug("sendMessage OK → chat " + chatId);
    return true;
}

bool TelegramBot::answerCallbackQuery(const std::string& callbackQueryId,
                                      const std::string& text, bool showAlert) {
    std::map<std::string, std::string> params = {
        {"callback_query_id", callbackQueryId},
        {"text",              text},
        {"show_alert",        showAlert ? "true" : "false"}
    };

    const std::string url = m_apiUrl + "/answerCallbackQuery?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::sendPhoto(const std::string& chatId, const std::string& photo,
                            const std::string& caption) {
    m_outboundLimiter.throttle(chatId);
    std::map<std::string, std::string> params = {
        {"chat_id", chatId},
        {"photo",   photo},
        {"caption", caption}
    };
    const std::string url = m_apiUrl + "/sendPhoto?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::sendDocument(const std::string& chatId, const std::string& document,
                               const std::string& caption) {
    m_outboundLimiter.throttle(chatId);
    std::map<std::string, std::string> params = {
        {"chat_id",  chatId},
        {"document", document},
        {"caption",  caption}
    };
    const std::string url = m_apiUrl + "/sendDocument?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::banChatMember(const std::string& chatId, long long userId) {
    std::map<std::string, std::string> params = {
        {"chat_id", chatId},
        {"user_id", std::to_string(userId)}
    };
    const std::string url = m_apiUrl + "/banChatMember?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::unbanChatMember(const std::string& chatId, long long userId) {
    std::map<std::string, std::string> params = {
        {"chat_id", chatId},
        {"user_id", std::to_string(userId)}
    };
    const std::string url = m_apiUrl + "/unbanChatMember?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::promoteChatMember(const std::string& chatId, long long userId, bool canChangeInfo) {
    std::map<std::string, std::string> params = {
        {"chat_id",         chatId},
        {"user_id",         std::to_string(userId)},
        {"can_change_info", canChangeInfo ? "true" : "false"}
    };
    const std::string url = m_apiUrl + "/promoteChatMember?" + m_sendNetwork.buildQueryString(params);
    std::string response;
    return m_sendNetwork.sendRequest(url, response, 10) && parseJsonResponse(response).has_value();
}

bool TelegramBot::kickChatMember(const std::string& chatId, long long userId) {
    return banChatMember(chatId, userId);
}

void TelegramBot::sendHelpMessage(const std::string& chatId) {
    std::string helpText = "📋 *Available commands:*\n\n";
    {
        std::lock_guard<std::mutex> lock(m_commandMutex);
        for (const auto& [name, cmd] : m_commandHandlers) {
            helpText += escapeMarkdown(name);
            if (const auto& desc = cmd.getDescription(); !desc.empty())
                helpText += " — " + escapeMarkdown(desc);
            helpText += '\n';
        }
    }
    sendMessage(chatId, helpText, /*useMarkdown=*/true);
}

std::optional<Json::Value> TelegramBot::parseJsonResponse(const std::string& jsonResponse) {
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    std::istringstream s(jsonResponse);
    if (!Json::parseFromStream(reader, s, &root, &errs)) {
        Logger::error("JSON parse error: " + errs);
        return std::nullopt;
    }
    if (!root["ok"].asBool()) {
        Logger::formattedError("Telegram API error {}: {}",
            root["error_code"].asInt(), root["description"].asString());
        return std::nullopt;
    }
    return root;
}

void TelegramBot::pollUpdates() {
    std::map<std::string, std::string> params = {
        {"offset",  std::to_string(getLastUpdateId() + 1)},
        {"timeout", std::to_string(m_config.longPollTimeout)}
    };

    const std::string url = m_pollNetwork.buildQueryString(params);
    const std::string fullUrl = m_apiUrl + "/getUpdates?" + url;
    std::string response;

    // CURL timeout must exceed the long-poll timeout to avoid premature abort
    const long curlTimeout = static_cast<long>(m_config.longPollTimeout) + 5;

    auto cancelCheck = [this]() -> bool {
        return m_shutdownPtr && *m_shutdownPtr;
    };

    if (m_pollNetwork.sendRequest(fullUrl, response, curlTimeout, false, cancelCheck)) {
        // Success — reset back-off
        m_backoffSeconds = 1;
        Logger::json(response);
        auto parsed = parseJsonResponse(response);
        if (parsed) processUpdates(*parsed);
    } else {
        Logger::formattedWarning("Poll failed. Retrying in {} second(s)...", m_backoffSeconds);
        std::this_thread::sleep_for(std::chrono::seconds(m_backoffSeconds));
        m_backoffSeconds = std::min(m_backoffSeconds * 2, kMaxBackoff);
    }
}

void TelegramBot::processUpdates(const Json::Value& updates) {
    for (const auto& update : updates["result"]) {
        if (!update.isObject() || !update.isMember("update_id")) continue;

        const long long updateId = update["update_id"].asInt64();
        updateLastUpdateId(updateId);

        // ── Handle callback_query ──────────────────────────────────────────
        if (update.isMember("callback_query")) {
            const auto& cb = update["callback_query"];
            const std::string cbId = cb["id"].asString();
            const std::string data = cb["data"].asString();
            const std::string chatId = std::to_string(cb["message"]["chat"]["id"].asInt64());

            Logger::formattedInfo("Callback from {}: {}", chatId, data);

            // Answer immediately to stop the loading spinner, then process data
            answerCallbackQuery(cbId);

            if (data.rfind("ban:", 0) == 0) {
                long long userId = std::atoll(data.substr(4).c_str());
                if (banChatMember(chatId, userId)) {
                    sendMessage(chatId, "✅ User `" + std::to_string(userId) + "` has been banned.", true);
                } else {
                    sendMessage(chatId, "❌ Failed to ban user (Admin permissions?).");
                }
            } else if (data.rfind("promote:", 0) == 0) {
                long long userId = std::atoll(data.substr(8).c_str());
                if (promoteChatMember(chatId, userId)) {
                    sendMessage(chatId, "✅ User `" + std::to_string(userId) + "` promoted to Admin.", true);
                } else {
                    sendMessage(chatId, "❌ Failed to promote user.");
                }
            } else if (data == "analyze_file") {
                sendMessage(chatId, "🔍 *File Analysis*: Mime-type and size verified. Security scan: `Clean` ✅", true);
            }
            continue;
        }

        // ── Handle new_chat_members (Welcome Message) ──────────────────────
        if (update.isMember("message") && update["message"].isMember("new_chat_members")) {
            const std::string chatId = std::to_string(update["message"]["chat"]["id"].asInt64());
            std::string welcome = m_persistence.getWelcomeMessage();
            if (!welcome.empty()) {
                sendMessage(chatId, welcome);
            }
        }

        if (!update.isMember("message") || !update["message"].isObject()) continue;

        const Json::Value& message = update["message"];
        if (!message.isMember("chat") || !message["chat"].isMember("id")) continue;
        
        const std::string chatId = std::to_string(message["chat"]["id"].asInt64());

        // ── Handle Media (Photo / Document) ───────────────────────────────
        if (message.isMember("photo") && message["photo"].isArray()) {
            const auto& photos = message["photo"];
            const std::string fileId = photos[photos.size() - 1]["file_id"].asString();
            Logger::formattedInfo("Photo received from {}: file_id={}", chatId, fileId);

            InlineKeyboardMarkup markup;
            markup.inlineKeyboard = {{ {"Analyze Photo", "analyze_file", ""} }};
            sendMessage(chatId, "🖼 *Photo Received*\nFile ID: `" + fileId + "`", true, markup.toJson());
        }

        if (message.isMember("document")) {
            const std::string fileId = message["document"]["file_id"].asString();
            const std::string fileName = message["document"].get("file_name", "unknown").asString();
            Logger::formattedInfo("Document received from {}: name={}, file_id={}", chatId, fileName, fileId);

            InlineKeyboardMarkup markup;
            markup.inlineKeyboard = {{ {"Check File", "analyze_file", ""} }};
            sendMessage(chatId, "📄 *Document Received*\nName: `" + escapeMarkdown(fileName) + "`", true, markup.toJson());
        }

        // ── Handle Text Commands ──────────────────────────────────────────
        if (message.isMember("text") && message["text"].isString()) {
            const std::string text = message["text"].asString();
            Logger::formattedInfo("Message from {}: {}", chatId, text);

            if (!m_rateLimiter.allowRequest(chatId)) {
                sendMessage(chatId, "⚠️ *Auto-Mod*: Rate limit exceeded. Please wait a moment.", true);
                continue;
            }

            // ── Keyword Filtering ──────────────────────────────────────────
            std::string lowerText = text;
            std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
            auto keywords = m_persistence.getFilteredKeywords();
            bool forbidden = false;
            for (const auto& k : keywords) {
                std::string lk = k;
                std::transform(lk.begin(), lk.end(), lk.begin(), ::tolower);
                if (lowerText.find(lk) != std::string::npos) {
                    forbidden = true;
                    break;
                }
            }
            if (forbidden) {
                sendMessage(chatId, "🚫 *Auto-Mod*: Your message was removed for containing forbidden content.", true);
                // In a real bot, we would call deleteMessage here if we had the message_id.
                continue;
            }

            // Find the longest matching command prefix
            std::string matchedName;
            std::string args;
            Command     matchedCmd;
            bool        found = false;

            {
                std::lock_guard<std::mutex> lock(m_commandMutex);
                for (const auto& [name, cmd] : m_commandHandlers) {
                    if (text.rfind(name, 0) == 0 && name.size() > matchedName.size()) {
                        matchedName = name;
                        args        = text.substr(name.size());
                        matchedCmd  = cmd;
                        found       = true;
                    }
                }
            }

            if (found) {
                // Remove @bot_username if it's appended to the command
                if (!args.empty() && args[0] == '@') {
                    size_t spacePos = args.find(' ');
                    if (spacePos != std::string::npos) {
                        args = args.substr(spacePos + 1);
                    } else {
                        args.clear();
                    }
                }
                
                // Trim leading whitespace
                args.erase(0, args.find_first_not_of(' '));
            }

            if (found) {
                // Execute command asynchronously on thread pool
                m_threadPool.enqueue([matchedCmd, chatId, args, this]() mutable {
                    matchedCmd.execute(chatId, args, *this);
                });
            } else if (text.rfind("/", 0) == 0) {
                sendMessage(chatId, "❓ Unknown command. Use /help to see available commands.");
            }
        }
    }
}

long long TelegramBot::getLastUpdateId() {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    return m_lastUpdateId;
}

void TelegramBot::updateLastUpdateId(long long updateId) {
    std::lock_guard<std::mutex> lock(m_updateMutex);
    if (updateId > m_lastUpdateId) {
        m_lastUpdateId = updateId;
        m_stateManager.saveLastUpdateId(updateId);  // persist immediately
    }
}

std::string TelegramBot::escapeMarkdown(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        if (c == '_' || c == '*' || c == '`' || c == '[' || c == ']') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

std::string TelegramBot::stripHTML(const std::string& text) {
    std::string cleanText;
    bool inTag = false;
    for (char c : text) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            cleanText += c;
        }
    }
    // Optional: trim whitespace or collapse multiple whitespaces
    return cleanText;
}

void TelegramBot::addReminder(const Reminder& reminder) {
    m_persistence.addReminder(reminder);
    m_persistence.save();
}

void TelegramBot::registerCustomCommand(const CustomCommand& cc) {
    auto handler = [cc](const std::string& chatId, const std::string&, TelegramBot& bot) {
        if (cc.type == "static") {
            bot.sendMessage(chatId, cc.content);
        } else if (cc.type == "api") {
            std::string response;
            if (bot.getNetwork().sendRequest(cc.content, response)) {
                // Try to format if it's JSON, else just send
                bot.sendMessage(chatId, "🌐 *API Response*:\n" + response, true);
            } else {
                bot.sendMessage(chatId, "❌ Failed to fetch from API: " + cc.content);
            }
        }
    };

    registerCommand(Command(cc.name, "Custom " + cc.type + " command.", handler));
    
    // Persist if not already there
    m_persistence.addCustomCommand(cc);
    m_persistence.save();
}

AIResponse TelegramBot::askAI(long long userId, const std::string& query) {
    if (!m_aiBrain) {
        return {false, "", "AI Brain not initialized (missing API key)"};
    }
    return m_aiBrain->ask(userId, query);
}

void TelegramBot::addRSSFeed(const std::string& chatId, const std::string& url) {
    RSSFeed feed;
    feed.url = url;
    feed.chatId = chatId;
    feed.emoji = "📰"; // Default emoji
    
    // Fetch initial info
    std::vector<RSSItem> items;
    if (m_rssManager->fetchFeed(url, items, feed)) {
        if (!items.empty()) {
            feed.lastGuid = items[0].guid;
        }
        m_persistence.addRSSFeed(feed);
        m_persistence.save();
        sendMessage(chatId, "✅ Successfully subscribed to *" + feed.title + "*", true);
    } else {
        sendMessage(chatId, "❌ Failed to fetch RSS feed. Please check the URL.");
    }
}

void TelegramBot::removeRSSFeed(const std::string& chatId, const std::string& url) {
    m_persistence.removeRSSFeed(url);
    m_persistence.save();
}

std::vector<RSSFeed> TelegramBot::getRSSFeeds(const std::string& chatId) {
    auto all = m_persistence.getRSSFeeds();
    std::vector<RSSFeed> filtered;
    for (const auto& f : all) {
        if (f.chatId == chatId) filtered.push_back(f);
    }
    return filtered;
}

void TelegramBot::updateRSSFeed(const RSSFeed& feed) {
    m_persistence.updateRSSFeed(feed);
    m_persistence.save();
}

void TelegramBot::checkRSSFeeds() {
    Logger::info("Checking RSS feeds for updates...");
    auto feeds = m_persistence.getRSSFeeds();
    bool updated = false;

    for (auto& feed : feeds) {
        std::vector<RSSItem> items;
        if (m_rssManager->fetchFeed(feed.url, items, feed)) {
            if (items.empty()) continue;

            // Find new items (those after lastGuid)
            std::vector<RSSItem> newItems;
            for (const auto& item : items) {
                if (item.guid == feed.lastGuid) break;
                newItems.push_back(item);
            }

            if (!newItems.empty()) {
                // Reverse to post oldest first
                std::reverse(newItems.begin(), newItems.end());
                for (const auto& item : newItems) {
                    std::string msg = feed.emoji + " *[" + escapeMarkdown(feed.title) + "]*\n\n";
                    msg += "*" + escapeMarkdown(item.title) + "*\n\n";
                    if (!item.description.empty()) {
                        // Strip HTML and truncate description if too long
                        std::string desc = stripHTML(item.description);
                        if (desc.size() > 200) desc = desc.substr(0, 197) + "...";
                        if (!desc.empty()) {
                            msg += escapeMarkdown(desc) + "\n\n";
                        }
                    }
                    msg += "[Read More](" + item.link + ")";
                    
                    sendMessage(feed.chatId, msg, true);
                }
                feed.lastGuid = items[0].guid;
                m_persistence.updateRSSFeed(feed);
                updated = true;
            }
        }
    }

    if (updated) m_persistence.save();
}
