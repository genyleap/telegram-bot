#include "telegrambot.hpp"
#include <algorithm>
#include <sstream>

RateLimiter::RateLimiter(int maxRequests, std::chrono::seconds interval)
    : maxRequests(maxRequests), interval(interval) {}

bool RateLimiter::allowRequest(const std::string& userId) {
    std::lock_guard<std::mutex> lock(rateLimiterMutex);
    auto now = std::chrono::steady_clock::now();
    auto& userRequests = userRequestTimestamps[userId];

    userRequests.erase(std::remove_if(userRequests.begin(), userRequests.end(),
                                      [now, this](const auto& timestamp) {
                                          return now - timestamp > interval;
                                      }), userRequests.end());

    if (userRequests.size() < maxRequests) {
        userRequests.push_back(now);
        return true;
    } else {
        return false;
    }
}

Command::Command() : name(""), handler(nullptr) {}

Command::Command(const std::string& name, std::function<void(const std::string&, const std::string&, TelegramBot&)> handler)
    : name(name), handler(handler) {}

std::string Command::getName() const {
    return name;
}

void Command::execute(const std::string& chatId, const std::string& args, TelegramBot& bot) const {
    if (handler) {
        handler(chatId, args, bot);
    } else {
        bot.sendMessage(chatId, "Command handler not set.");
    }
}

TelegramBot::TelegramBot(const std::string& token)
    : token(token), apiUrl("https://api.telegram.org/bot" + token), lastUpdateId(0),
    rateLimiter(5, std::chrono::seconds(10)) {}

void TelegramBot::registerCommand(const Command& command) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandHandlers[command.getName()] = command;
}

void TelegramBot::start() {
    Logger::info("Bot started. Polling for updates...");
    while (true) {
        pollUpdates();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void TelegramBot::sendMessage(const std::string& chatId, const std::string& text, bool useMarkdown) {
    std::string encodedText = urlEncode(text);
    std::string url = apiUrl + "/sendMessage?chat_id=" + chatId + "&text=" + encodedText;

    // Add parse_mode if Markdown is enabled
    if (useMarkdown) {
        url += "&parse_mode=Markdown";
    }

    std::string response;
    if (network.sendRequest(url, response)) {
        Logger::json(response);
    } else {
        Logger::error("Failed to send message.");
    }
}

std::string TelegramBot::urlEncode(const std::string& text) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        Logger::error("Failed to initialize CURL for URL encoding.");
        return {};
    }

    char* encodedText = curl_easy_escape(curl, text.c_str(), static_cast<int>(text.length()));
    if (!encodedText) {
        Logger::error("Failed to URL-encode text.");
        curl_easy_cleanup(curl);
        return {};
    }

    std::string result(encodedText);
    curl_free(encodedText);
    curl_easy_cleanup(curl);
    return result;
}

std::optional<Json::Value> TelegramBot::parseJsonResponse(const std::string& jsonResponse) {
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;

    std::istringstream s(jsonResponse);
    if (!Json::parseFromStream(reader, s, &root, &errs)) {
        Logger::error("Failed to parse JSON: " + errs);
        return std::nullopt;
    }

    if (!root["ok"].asBool()) {
        Logger::error("Telegram API error: " + root["description"].asString());
        return std::nullopt;
    }

    return root;
}

void TelegramBot::pollUpdates() {
    std::string url = apiUrl + "/getUpdates?offset=" + std::to_string(getLastUpdateId() + 1);
    std::string response;

    if (network.sendRequest(url, response)) {
        Logger::json(response);
        auto jsonResponse = parseJsonResponse(response);

        if (jsonResponse) {
            processUpdates(*jsonResponse);
        }
    } else {
        Logger::error("No response from Telegram API.");
    }
}

void TelegramBot::processUpdates(const Json::Value& updates) {
    for (const auto& update : updates["result"]) {
        if (!update.isObject() || !update.isMember("update_id")) {
            continue;
        }

        long long updateId = update["update_id"].asInt64();
        updateLastUpdateId(updateId);

        if (!update.isMember("message") || !update["message"].isObject()) {
            continue;
        }

        const Json::Value& message = update["message"];
        if (!message.isMember("chat") || !message["chat"].isObject() || !message["chat"].isMember("id")) {
            continue;
        }

        if (!message.isMember("text") || !message["text"].isString()) {
            continue;
        }

        std::string chatId = std::to_string(message["chat"]["id"].asInt64());
        std::string text = message["text"].asString();

        Logger::formattedInfo("Received message: {} from chat ID: {}", text, chatId);

        if (!rateLimiter.allowRequest(chatId)) {
            sendMessage(chatId, "Rate limit exceeded. Please try again later.");
            continue;
        }

        // Find the longest matching command
        std::string command;
        std::string args;
        Command matchedCommand;
        bool hasMatchedCommand = false;

        {
            std::lock_guard<std::mutex> lock(commandMutex);
            for (const auto& [cmdName, registeredCommand] : commandHandlers) {
                if (text.rfind(cmdName, 0) == 0) { // Check if text starts with cmdName
                    if (cmdName.length() > command.length()) { // Prefer the longest match
                        command = cmdName;
                        args = text.substr(cmdName.length());
                        matchedCommand = registeredCommand;
                        hasMatchedCommand = true;
                    }
                }
            }
        }

        // Trim leading whitespace from args
        args.erase(0, args.find_first_not_of(' '));

        if (hasMatchedCommand) {
            matchedCommand.execute(chatId, args, *this);
        } else {
            sendMessage(chatId, "Unknown command. Use /help to see available commands.");
        }
    }
}

long long TelegramBot::getLastUpdateId() {
    std::lock_guard<std::mutex> lock(updateMutex);
    return lastUpdateId;
}

void TelegramBot::updateLastUpdateId(long long updateId) {
    std::lock_guard<std::mutex> lock(updateMutex);
    if (updateId > lastUpdateId) {
        lastUpdateId = updateId;
    }
}
