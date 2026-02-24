#include "telegrambot.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>

// Example command: /time
void handleTimeCommand(const std::string& chatId, const std::string&, TelegramBot& bot) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    bot.sendMessage(chatId, "Current date and time: " + ss.str());
}

// Example command: /echo
void handleEchoCommand(const std::string& chatId, const std::string& args, TelegramBot& bot) {
    if (args.empty()) {
        bot.sendMessage(chatId, "Usage: /echo <message>");
    } else {
        bot.sendMessage(chatId, "You said: " + args);
    }
}

int main(int argc, char* argv[]) {
    std::string botToken;
    if (argc > 1) {
        botToken = argv[1];
    } else if (const char* tokenEnv = std::getenv("TELEGRAM_BOT_TOKEN")) {
        botToken = tokenEnv;
    }

    if (botToken.empty()) {
        std::cerr << "Usage: " << argv[0] << " <BOT_TOKEN>\n"
                  << "Or set TELEGRAM_BOT_TOKEN environment variable.\n";
        return 1;
    }

    TelegramBot bot(botToken);

    // Register commands dynamically
    bot.registerCommand(Command("/time", handleTimeCommand));
    bot.registerCommand(Command("/echo", handleEchoCommand));

    // Start the bot
    bot.start();

    return 0;
}
