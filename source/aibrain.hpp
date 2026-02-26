#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "network.hpp"

struct AIResponse {
    bool success;
    std::string text;
    std::string error;
};

struct ChatMessage {
    std::string role; // "user", "assistant", "system"
    std::string content;
};

class AIProvider {
public:
    virtual ~AIProvider() = default;
    virtual AIResponse chat(const std::vector<ChatMessage>& history) = 0;
    virtual std::string getName() const = 0;
};

class AIBrain {
public:
    AIBrain(Network& network, const std::string& apiKey, const std::string& provider = "openai");
    
    AIResponse ask(long long userId, const std::string& query);
    void clearHistory(long long userId);
    
    void setSystemPrompt(const std::string& prompt);

private:
    Network& m_network;
    std::string m_apiKey;
    std::string m_systemPrompt;
    std::unique_ptr<AIProvider> m_provider;
    
    struct UserContext {
        std::vector<ChatMessage> history;
    };
    std::map<long long, UserContext> m_contexts;
    
    static constexpr size_t MAX_HISTORY = 10;
};
