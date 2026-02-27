#include "telegrambot.hpp"
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <json/json.h>
#include <vector>
#include "sysinfo.hpp"
#include "aibrain.hpp"
#include "calculator.hpp"

// ── Graceful-shutdown flag ─────────────────────────────────────────────────
namespace {
    volatile std::sig_atomic_t g_shutdown = 0;
    void signalHandler(int) { g_shutdown = 1; }
    SysInfo g_sysInfo;
}

// ── Config loader ──────────────────────────────────────────────────────────
BotConfig loadConfig(const std::string& defaultPath = "config/system-config.json") {
    BotConfig cfg;
    std::string actualPath;
    
    // Check possible locations relative to the executable
    const std::vector<std::string> searchPaths = {
        defaultPath,                     // Same dir or relative from root
        "../" + defaultPath,             // One level up
        "../../" + defaultPath,          // Two levels up (typical from build/macOS)
        "../../../" + defaultPath,       // Three levels up (typical from build/macOS/TelegramBot.app)
        "../../../../" + defaultPath,    // Four levels up (from MacOS/ folder)
        "../../../../../" + defaultPath, // Five levels up
        "../Resources/" + defaultPath    // Inside macOS App Bundle Resources
    };

    std::ifstream ifs;
    for (const auto& p : searchPaths) {
        ifs.open(p);
        if (ifs.is_open()) {
            actualPath = p;
            break;
        }
    }

    if (actualPath.empty()) {
        std::cerr << "[WARN] Config file not found, using defaults.\n";
        return cfg;
    }
    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    if (!Json::parseFromStream(reader, ifs, &root, &errs)) {
        std::cerr << "[WARN] Failed to parse config: " << errs << " — using defaults.\n";
        return cfg;
    }
    cfg.debug           = root.get("debug",            false).asBool();
    cfg.token           = root.get("token",            "").asString();
    cfg.threadPoolSize  = root.get("thread_pool_size", 4).asInt();
    cfg.logFile         = root.get("log_file",         "").asString();
    cfg.stateFile       = root.get("state_file",       ".bot_state").asString();
    cfg.longPollTimeout = root.get("long_poll_timeout", 30).asInt();
    cfg.rssCheckInterval = root.get("rss_check_interval", 900).asInt();
    cfg.aiApiKey        = root.get("ai_api_key",       "").asString();

    // Webhook section
    const Json::Value& wh = root["webhook"];
    cfg.webhookMode  = wh.get("enabled", false).asBool();
    cfg.webhookUrl   = wh.get("url",     "").asString();
    cfg.webhookPath  = wh.get("path",    "/webhook").asString();
    cfg.webhookPort  = wh.get("port",    8443).asInt();

    return cfg;
}

// ── Command handlers ───────────────────────────────────────────────────────
void handleTimeCommand(const std::string& chatId, const std::string&, TelegramBot& bot) {
    auto        now = std::chrono::system_clock::now();
    std::time_t t   = std::chrono::system_clock::to_time_t(now);
    std::tm     tm_buf{};
    localtime_r(&t, &tm_buf);
    std::stringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    bot.sendMessage(chatId, "🕐 Current date & time: " + ss.str());
}

void handleEchoCommand(const std::string& chatId, const std::string& args, TelegramBot& bot) {
    if (args.empty())
        bot.sendMessage(chatId, "Usage: /echo <message>");
    else
        bot.sendMessage(chatId, "🔁 You said: " + args);
}

void handleMenuCommand(const std::string& chatId, const std::string&, TelegramBot& bot) {
    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = {
        { {"Google", "", "https://google.com"}, {"Bing", "", "https://bing.com"} },
        { {"Click Me!", "button_clicked", ""} }
    };
    bot.sendMessage(chatId, "🛠 *Main Menu*\nSelect an option below:", true, markup.toJson());
}

void handleStatusCommand(const std::string& chatId, const std::string&, TelegramBot& bot) {
    bot.sendMessage(chatId, g_sysInfo.getStatusString(), true);
}

void handleIdCommand(const std::string& chatId, const std::string&, TelegramBot& bot) {
    std::stringstream ss;
    ss << "🆔 *User Identity Card*\n"
       << "━━━━━━━━━━━━━━━\n"
       << "👤 *Your ID*: `" << chatId << "`\n"
       << "📍 *Chat ID*: `" << chatId << "`\n"
       << "🛡 *Status*: `Active Member`";
    bot.sendMessage(chatId, ss.str(), true);
}

void handleRemindMeCommand(const std::string& chatId, const std::string& args, TelegramBot& bot) {
    if (args.empty()) {
        bot.sendMessage(chatId, "⏰ Usage: `/remindme <time> <message>`\nExample: `/remindme 10s walk the dog`", true);
        return;
    }

    std::stringstream ss(args);
    std::string timeStr;
    ss >> timeStr;

    std::string message;
    std::getline(ss, message);
    if (!message.empty() && message[0] == ' ') message.erase(0, 1);

    long long seconds = 0;
    if (timeStr.back() == 's') seconds = std::atoll(timeStr.substr(0, timeStr.size()-1).c_str());
    else if (timeStr.back() == 'm') seconds = std::atoll(timeStr.substr(0, timeStr.size()-1).c_str()) * 60;
    else if (timeStr.back() == 'h') seconds = std::atoll(timeStr.substr(0, timeStr.size()-1).c_str()) * 3600;
    else seconds = std::atoll(timeStr.c_str());

    if (seconds <= 0) {
        bot.sendMessage(chatId, "❌ Invalid time format. Use e.g. `10s`, `5m`, `1h`.");
        return;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    Reminder rem;
    rem.chatId = chatId;
    rem.message = message.empty() ? "Alert!" : message;
    rem.triggerTime = now + seconds;

    bot.addReminder(rem);
    bot.sendMessage(chatId, "✅ Reminder set for " + timeStr + " from now.");
}

void handleConvertCommand(const std::string& chatId, const std::string& args, TelegramBot& bot) {
    if (args.empty()) {
        bot.sendMessage(chatId, "💱 Usage: `/convert <amount> <from> <to>`\nExample: `/convert 100 USD EUR` or `/convert 1 BTC USD`", true);
        return;
    }

    std::stringstream ss(args);
    double amount;
    std::string from, to;
    if (!(ss >> amount >> from >> to)) {
        bot.sendMessage(chatId, "❌ Invalid format. Use: `/convert 100 USD EUR`", true);
        return;
    }

    std::transform(from.begin(), from.end(), from.begin(), ::toupper);
    std::transform(to.begin(), to.end(), to.begin(), ::toupper);

    // Using Coinbase public API for exchange rates
    std::string url = "https://api.coinbase.com/v2/exchange-rates?currency=" + from;
    std::string response;
    Network network;
    if (network.sendRequest(url, response, 60)) {
        Json::Value root;
        Json::CharReaderBuilder reader;
        std::string errs;
        std::istringstream s(response);
        if (Json::parseFromStream(reader, s, &root, &errs)) {
            if (root.isMember("data") && root["data"].isMember("rates")) {
                const Json::Value& rates = root["data"]["rates"];
                if (rates.isMember(to)) {
                    double rate = std::stod(rates[to].asString());
                    double result = amount * rate;
                    std::stringstream resSS;
                    resSS << "💱 *" << amount << " " << from << "* = `" << std::fixed << std::setprecision(2) << result << " " << to << "`";
                    bot.sendMessage(chatId, resSS.str(), true);
                    return;
                }
            }
        }
    }
    bot.sendMessage(chatId, "❌ Failed to fetch rates or invalid currency code.");
}

void handleAdminCommand(const std::string& chatId, const std::string& args, TelegramBot& bot) {
    if (args.empty()) {
        bot.sendMessage(chatId, "⚠️ Usage: `/admin <user_id>`", true);
        return;
    }
    long long userId = std::atoll(args.c_str());
    if (userId == 0) {
        bot.sendMessage(chatId, "❌ Invalid User ID.");
        return;
    }
    InlineKeyboardMarkup markup;
    markup.inlineKeyboard = {
        { {"Ban", std::string("ban:") + args, ""}, {"Promote", std::string("promote:") + args, ""} }
    };
    bot.sendMessage(chatId, "🛡 *Admin Action*: Manage user `" + args + "`", true, markup.toJson());
}

// ── Entry point ───────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    BotConfig config = loadConfig();
    std::string botToken;

    // Smart argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [TOKEN] [OPTIONS]\n"
                      << "Options:\n"
                      << "  --help, -h       Show this help message\n"
                      << "  <TOKEN>          Bot API token (overrides config/env)\n\n"
                      << "Config: Edit config/system-config.json to set token, webhook, etc.\n";
            return 0;
        }
        if (arg.rfind("-", 0) != 0 && botToken.empty()) {
            botToken = arg; // First non-flag argument is the token
        }
    }

    if (botToken.empty() && !config.token.empty()) {
        botToken = config.token;
    } else if (botToken.empty()) {
        if (const char* env = std::getenv("TELEGRAM_BOT_TOKEN")) {
            botToken = env;
        }
    }

    if (botToken.empty()) {
        std::cerr << "Error: No bot token provided. Set it in config, env, or as an argument.\n"
                  << "Run with --help for usage information.\n";
        return 1;
    }

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    TelegramBot bot(botToken, config);

    bot.registerCommand(Command("/time",     "Shows the current date and time.", handleTimeCommand));
    bot.registerCommand(Command("/echo",     "Echoes your message back. Usage: /echo <text>", handleEchoCommand));
    bot.registerCommand(Command("/status",   "Show system health metrics.", handleStatusCommand));
    bot.registerCommand(Command("/id",       "Show your unique Telegram IDs.", handleIdCommand));
    bot.registerCommand(Command("/remindme", "Set a timer. Usage: /remindme 10s walk", handleRemindMeCommand));
    bot.registerCommand(Command("/convert",  "Currency/Crypto converter. Example: /convert 1 BTC USD", handleConvertCommand));
    bot.registerCommand(Command("/admin",    "Administrative tools (Admin only).", handleAdminCommand));
    bot.registerCommand(Command("/menu",     "Show an inline keyboard menu.", handleMenuCommand));
    bot.registerCommand(Command("/ai",       "Ask the AI brain. Example: /ai how are you?", [](const std::string& chatId, const std::string& args, TelegramBot& b) {
        if (args.empty()) {
            b.sendMessage(chatId, "🤖 Usage: `/ai <your question>`", true);
            return;
        }
        b.sendMessage(chatId, "⏳ *AI is thinking...*", true);
        
        AIResponse resp = b.askAI(std::atoll(chatId.c_str()), args);
        if (resp.success) {
            b.sendMessage(chatId, "🤖 " + resp.text, true);
        } else {
            b.sendMessage(chatId, "❌ *AI Error*: " + resp.error, true);
        }
    }));

    bot.registerCommand(Command("/setwelcome", "Set a welcome message for new members.", [](const std::string& chatId, const std::string& args, TelegramBot& b) {
        if (args.empty()) {
            b.sendMessage(chatId, "📝 Usage: `/setwelcome <message>`", true);
            return;
        }
        b.setWelcomeMessage(args);
        b.sendMessage(chatId, "✅ Welcome message set!", true);
    }));

    bot.registerCommand(Command("/addfilter", "Add a word to the forbidden list.", [](const std::string& chatId, const std::string& args, TelegramBot& b) {
        if (args.empty()) {
            b.sendMessage(chatId, "🚫 Usage: `/addfilter <word>`", true);
            return;
        }
        b.addFilteredKeyword(args);
        b.sendMessage(chatId, "✅ Word `" + args + "` added to filter.", true);
    }));

    bot.registerCommand(Command("/listfilters", "List all forbidden words.", [](const std::string& chatId, const std::string&, TelegramBot& b) {
        auto keywords = b.getFilteredKeywords();
        if (keywords.empty()) {
            b.sendMessage(chatId, "Filters are empty.");
            return;
        }
        std::string list = "🚫 *Forbidden Words*:\n";
        for (const auto& k : keywords) list += "- " + k + "\n";
        b.sendMessage(chatId, list, true);
    }));

    bot.registerCommand(Command("/calc",     "Advanced calculator. Example: /calc sqrt(16) + 2^3", [](const std::string& chatId, const std::string& args, TelegramBot& b) {
        if (args.empty()) {
            b.sendMessage(chatId, "🧮 Usage: `/calc <expression>`", true);
            return;
        }
        
        auto res = Calculator::evaluate(args);
        if (res.success) {
            std::string msg = "🧮 *Result*: `" + std::to_string(res.value) + "`";
            // Strip trailing zeros for prettier output if it's a "clean" number
            if (res.value == (long long)res.value) {
                msg = "🧮 *Result*: `" + std::to_string((long long)res.value) + "`";
            }
            b.sendMessage(chatId, msg, true);
        } else {
            b.sendMessage(chatId, "❌ *Calc Error*: " + res.error, true);
        }
    }));

    if (config.webhookMode) {
        Logger::info("Starting in webhook mode.");
        bot.startWebhook(g_shutdown);
    } else {
        Logger::info("Starting in long-polling mode.");
        bot.start(g_shutdown);
    }

    Logger::info("Bot shut down cleanly.");
    return 0;
}
