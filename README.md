# Telegram Bot in C++

This repository contains a **Telegram Bot** implemented in C++. The bot is designed to handle user commands, enforce rate limits, and interact with the Telegram API to send and receive messages. It is a robust and scalable solution for building custom Telegram bots with advanced features.

---

## Features

- **Command Handling**: Register and execute custom commands dynamically.
- **Rate Limiting**: Prevent abuse by limiting user requests within a specified time interval.
- **Telegram API Integration**: Send and receive messages with optional Markdown formatting.
- **Thread Safety**: Ensures safe access to shared resources using mutexes.
- **JSON Parsing**: Processes Telegram API responses using JsonCpp.
- **Logging**: Provides detailed logs for debugging and monitoring bot activity.

---

## Getting Started

### Prerequisites

- **libcurl**: For HTTP requests and URL encoding.
- **JsonCpp**: For parsing JSON responses.
- **C++ Compiler**: Supports C++11 or later.

---

## Installation

### Linux

1. Clone the repository:
   ```bash
   git clone https://github.com/genyleap/telegram-bot
   cd telegram-bot
   ```

2. Install dependencies:
   ```bash
   sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev
   ```

3. Build the project:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

4. Run the bot:
   ```bash
   ./TelegramBot YOUR_TELEGRAM_BOT_TOKEN
   ```

---

### Windows

1. Clone the repository:
   ```bash
   git clone https://github.com/genyleap/telegram-bot
   cd telegram-bot
   ```

2. Install dependencies:
   - Download and install [vcpkg](https://vcpkg.io/) for managing dependencies.
   - Install `libcurl` and `jsoncpp` using vcpkg:
     ```bash
     vcpkg install curl jsoncpp
     ```

3. Build the project:
   - Open the project in Visual Studio or use CMake:
     ```bash
     mkdir build
     cd build
     cmake -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake ..
     cmake --build .
     ```

4. Run the bot:
   ```bash
   .\TelegramBot YOUR_TELEGRAM_BOT_TOKEN
   ```

---

### macOS

1. Clone the repository:
   ```bash
   git clone https://github.com/genyleap/telegram-bot
   cd telegram-bot
   ```

2. Install dependencies:
   ```bash
   brew install curl jsoncpp
   ```

3. Build the project:
   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

4. Run the bot:
   ```bash
   ./TelegramBot YOUR_TELEGRAM_BOT_TOKEN
   ```

---

## Usage

### Registering Commands

You can register custom commands with the bot:
```cpp
TelegramBot bot("YOUR_TELEGRAM_BOT_TOKEN");

bot.registerCommand(Command("start", [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
    bot.sendMessage(chatId, "Welcome to the bot!");
}));

bot.registerCommand(Command("help", [](const std::string& chatId, const std::string& args, TelegramBot& bot) {
    bot.sendMessage(chatId, "Available commands: /start, /help");
}));

bot.start();
```

### Sending Messages

The bot can send messages with optional Markdown formatting:
```cpp
bot.sendMessage(chatId, "Hello, world!", true); // Markdown enabled
```

---

## Configuration

- **Rate Limiting**: Adjust the rate limit settings in the `RateLimiter` constructor:
  ```cpp
  RateLimiter rateLimiter(5, std::chrono::seconds(10)); // 5 requests per 10 seconds
  ```

- **Logging**: Customize logging levels and output as needed.

---

## Example Commands

- `/start`: Welcomes the user.
- `/help`: Lists available commands.
- Add custom commands to suit your bot's functionality.

---

## Contributing

Contributions are welcome! Please open an issue or submit a pull request for any improvements, bug fixes, or new features.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

- **libcurl**: For handling HTTP requests.
- **JsonCpp**: For parsing JSON responses.
- **Telegram API**: For providing the bot infrastructure.

---

Enjoy building your Telegram bot with this C++ implementation! 🚀
