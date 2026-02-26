#include "aibrain.hpp"
#include "logger.hpp"
#include <json/json.h>

class OpenAIProvider : public AIProvider {
public:
    OpenAIProvider(Network& network, const std::string& apiKey) 
        : m_network(network), m_apiKey(apiKey) {}

    AIResponse chat(const std::vector<ChatMessage>& history) override {
        std::string url = "https://api.openai.com/v1/chat/completions";
        
        Json::Value root;
        root["model"] = "gpt-4o";
        Json::Value messages(Json::arrayValue);
        
        for (const auto& msg : history) {
            Json::Value m;
            m["role"] = msg.role;
            m["content"] = msg.content;
            messages.append(m);
        }
        root["messages"] = messages;

        Json::StreamWriterBuilder writer;
        std::string requestBody = Json::writeString(writer, root);
        
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + m_apiKey}
        };

        std::string responseBody;
        if (m_network.postRequest(url, requestBody, responseBody, 60, false, headers)) {
            Json::Value respJson;
            Json::CharReaderBuilder reader;
            std::string errs;
            std::istringstream s(responseBody);
            if (Json::parseFromStream(reader, s, &respJson, &errs)) {
                if (respJson.isMember("choices") && respJson["choices"].isArray() && !respJson["choices"].empty()) {
                    std::string text = respJson["choices"][0]["message"]["content"].asString();
                    return {true, text, ""};
                }
                if (respJson.isMember("error")) {
                    return {false, "", respJson["error"]["message"].asString()};
                }
            }
            return {false, "", "Failed to parse AI response"};
        }
        
        return {false, "", "Network request to AI provider failed"};
    }

    std::string getName() const override { return "OpenAI"; }

private:
    Network& m_network;
    std::string m_apiKey;
};

AIBrain::AIBrain(Network& network, const std::string& apiKey, const std::string& provider)
    : m_network(network), m_apiKey(apiKey) {
    
    if (provider == "openai") {
        m_provider = std::make_unique<OpenAIProvider>(network, apiKey);
    }
    
    m_systemPrompt = "You are a helpful Telegram bot assistant.";
}

AIResponse AIBrain::ask(long long userId, const std::string& query) {
    if (!m_provider) return {false, "", "No AI provider configured"};

    auto& context = m_contexts[userId];
    
    // Add system prompt if empty
    if (context.history.empty()) {
        context.history.push_back({"system", m_systemPrompt});
    }

    context.history.push_back({"user", query});
    
    // Limit history
    if (context.history.size() > MAX_HISTORY) {
        context.history.erase(context.history.begin() + 1); // Keep system prompt
    }

    auto resp = m_provider->chat(context.history);
    if (resp.success) {
        context.history.push_back({"assistant", resp.text});
    }
    
    return resp;
}

void AIBrain::clearHistory(long long userId) {
    m_contexts.erase(userId);
}

void AIBrain::setSystemPrompt(const std::string& prompt) {
    m_systemPrompt = prompt;
}
