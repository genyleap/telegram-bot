#ifndef WEBHOOK_SERVER_HPP
#define WEBHOOK_SERVER_HPP

/**
 * @file webhookserver.hpp
 * @brief Minimal POSIX HTTP server that receives Telegram webhook updates.
 *
 * Architecture:
 *   Telegram → HTTPS → nginx/caddy (TLS termination) → HTTP → WebhookServer
 *
 * The server listens on a plain TCP port. TLS MUST be terminated by a
 * reverse proxy (nginx, caddy, etc.) before reaching this server.
 * Telegram requires HTTPS on port 443, 80, 88 or 8443.
 */

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include "logger.hpp"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

/**
 * @class WebhookServer
 * @brief Listens on a TCP port for incoming POST requests from Telegram.
 *
 * Only handles the single webhook route (POST /webhook).
 * All other paths/methods receive a 404 or 405 response immediately.
 */
class WebhookServer {
public:
    using UpdateCallback = std::function<void(const std::string& jsonBody)>;

    /**
     * @brief Constructs a WebhookServer.
     * @param port      TCP port to listen on (e.g., 8443).
     * @param path      URL path to accept updates on (e.g., "/webhook").
     * @param callback  Called with the raw JSON body of each incoming update.
     */
    WebhookServer(int port, std::string path, UpdateCallback callback)
        : m_port(port), m_path(std::move(path)), m_callback(std::move(callback)) {}

    /**
     * @brief Starts the server in a background thread.
     * @return true if the server socket was created successfully.
     */
    bool start();

    /**
     * @brief Signals the server to stop and waits for the thread to exit.
     */
    void stop();

private:
    int          m_port;
    std::string  m_path;
    UpdateCallback m_callback;

    std::atomic<bool> m_running{false};
    int               m_serverFd{-1};
    std::thread       m_workerThread;

    void acceptLoop();
    void handleClient(int clientFd) const;

    static std::string readBody(int fd, std::size_t contentLength);
    static void        sendResponse(int fd, int status, const char* body);
};

#endif // WEBHOOK_SERVER_HPP
