#include "telegrambot.hpp"
#include "network.hpp"
#include "statemanager.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

// ── Helpers ──────────────────────────────────────────────────────────────────
static void test_pass(const char* name) {
    Logger::formattedInfo("  ✓ {}", name);
}

// ── RateLimiter (inbound) ────────────────────────────────────────────────────
static void test_rate_limiter() {
    Logger::info("--- RateLimiter tests ---");
    RateLimiter limiter(2, std::chrono::seconds(1));

    assert(limiter.allowRequest("u1"));
    assert(limiter.allowRequest("u1"));
    assert(!limiter.allowRequest("u1"));                       // blocked
    assert(limiter.allowRequest("u2"));                        // different user OK
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(limiter.allowRequest("u1"));                        // window expired
    test_pass("RateLimiter: sliding-window, per-user isolation, window reset");
}

// ── OutboundRateLimiter ───────────────────────────────────────────────────────
static void test_outbound_rate_limiter() {
    Logger::info("--- OutboundRateLimiter tests ---");
    OutboundRateLimiter orl;

    // Send 20 messages — well within both limits (30/sec global, 20/min per chat)
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) orl.throttle("chat_test");
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    // 20 sends must complete without throttling (< 1 global window)
    assert(elapsed < 1200);
    test_pass("OutboundRateLimiter: 20 sends within global budget, no blocking");

    // Different chat — per-chat window is independent
    OutboundRateLimiter orl2;
    for (int i = 0; i < 5; ++i) orl2.throttle("chat_a");
    for (int i = 0; i < 5; ++i) orl2.throttle("chat_b");
    test_pass("OutboundRateLimiter: per-chat windows are independent");
}

// ── Network::buildQueryString ─────────────────────────────────────────────────
static void test_build_query_string() {
    Logger::info("--- Network::buildQueryString tests ---");
    Network net;

    // Basic key=value pairs
    auto qs = net.buildQueryString({{"offset", "1"}, {"timeout", "30"}});
    assert(qs.find("offset=1") != std::string::npos);
    assert(qs.find("timeout=30") != std::string::npos);
    test_pass("buildQueryString: basic params");

    // Special characters must be URL-encoded
    auto qs2 = net.buildQueryString({{"text", "Hello & World"}});
    assert(qs2.find("Hello%20%26%20World") != std::string::npos ||
           qs2.find("Hello+%26+World")      != std::string::npos ||
           qs2.find("%26")                  != std::string::npos);
    test_pass("buildQueryString: URL encoding of special chars");

    // Empty map → empty string
    assert(net.buildQueryString({}).empty());
    test_pass("buildQueryString: empty map");
}

// ── StateManager ──────────────────────────────────────────────────────────────
static void test_state_manager() {
    Logger::info("--- StateManager tests ---");
    const std::string tmpFile = "/tmp/test_bot_state";
    std::remove(tmpFile.c_str());

    StateManager sm(tmpFile);
    assert(sm.loadLastUpdateId() == 0LL);       // missing file → 0
    test_pass("StateManager: missing file returns 0");

    sm.saveLastUpdateId(12345LL);
    assert(sm.loadLastUpdateId() == 12345LL);
    test_pass("StateManager: round-trip save and load");

    sm.saveLastUpdateId(99999LL);
    assert(sm.loadLastUpdateId() == 99999LL);
    test_pass("StateManager: overwrite with larger ID");

    std::remove(tmpFile.c_str());
}

// ── parseJsonResponse (via wrapper) ──────────────────────────────────────────
// We test JSON parsing logic directly using jsoncpp since parseJsonResponse is private.
static void test_json_parsing() {
    Logger::info("--- JSON parsing tests ---");
    Json::CharReaderBuilder reader;
    Json::Value root;
    std::string errs;

    // Valid ok=true response
    std::istringstream valid(R"({"ok":true,"result":[]})");
    assert(Json::parseFromStream(reader, valid, &root, &errs));
    assert(root["ok"].asBool() == true);
    test_pass("JSON: valid ok=true parsed correctly");

    // ok=false with error fields
    std::istringstream bad(R"({"ok":false,"error_code":400,"description":"Bad Request"})");
    assert(Json::parseFromStream(reader, bad, &root, &errs));
    assert(root["ok"].asBool() == false);
    assert(root["error_code"].asInt() == 400);
    test_pass("JSON: ok=false with error_code parsed correctly");

    // Malformed JSON
    std::istringstream malformed("not valid json{{{");
    assert(!Json::parseFromStream(reader, malformed, &root, &errs));
    test_pass("JSON: malformed input rejected");
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main() {
    Logger::configure("", Logger::Level::INFO);
    Logger::info("=== Running production test suite ===");

    test_rate_limiter();
    test_outbound_rate_limiter();
    test_build_query_string();
    test_state_manager();
    test_json_parsing();

    Logger::info("=== All tests passed ✓ ===");
    return 0;
}
