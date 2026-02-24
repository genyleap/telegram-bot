# Telegram Bot in C++

A Telegram bot implementation in modern C++ with command routing, rate limiting, JSON parsing, and libcurl-based Telegram API calls.

## Requirements

- CMake 3.23+
- C++23 compiler
- libcurl
- jsoncpp

## Build

Use a clean build directory:

```bash
cmake -S . -B build
cmake --build build
```

## Run

Provide your bot token as an argument:

```bash
./build/macOS/TelegramBot.app/Contents/MacOS/TelegramBot <BOT_TOKEN>
```

Or set an environment variable:

```bash
export TELEGRAM_BOT_TOKEN=<BOT_TOKEN>
./build/macOS/TelegramBot.app/Contents/MacOS/TelegramBot
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Current test coverage includes a `RateLimiter` unit test.

## Usage Example

```cpp
TelegramBot bot("YOUR_TELEGRAM_BOT_TOKEN");

bot.registerCommand(Command("/start", [](const std::string& chatId, const std::string&, TelegramBot& b) {
    b.sendMessage(chatId, "Welcome to the bot!");
}));

bot.registerCommand(Command("/help", [](const std::string& chatId, const std::string&, TelegramBot& b) {
    b.sendMessage(chatId, "Available commands: /start, /help");
}));

bot.start();
```
