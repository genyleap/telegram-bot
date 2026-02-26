#include "webhookserver.hpp"
#include <sstream>
#include <stdexcept>
#include <cerrno>

bool WebhookServer::start() {
    m_serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        Logger::error("WebhookServer: socket() failed: " + std::string(strerror(errno)));
        return false;
    }

    // Allow rapid restart without "Address already in use"
    int opt = 1;
    ::setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(m_port));

    if (::bind(m_serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Logger::error("WebhookServer: bind() failed on port " + std::to_string(m_port) +
                      ": " + strerror(errno));
        ::close(m_serverFd);
        return false;
    }

    if (::listen(m_serverFd, /*backlog=*/64) < 0) {
        Logger::error("WebhookServer: listen() failed.");
        ::close(m_serverFd);
        return false;
    }

    m_running = true;
    m_workerThread = std::thread(&WebhookServer::acceptLoop, this);
    Logger::formattedInfo("WebhookServer: listening on port {} at path {}", m_port, m_path);
    return true;
}

void WebhookServer::stop() {
    m_running = false;
    if (m_serverFd >= 0) ::close(m_serverFd);
    if (m_workerThread.joinable()) m_workerThread.join();
    Logger::info("WebhookServer: stopped.");
}

void WebhookServer::acceptLoop() {
    while (m_running) {
        sockaddr_in clientAddr{};
        socklen_t   len = sizeof(clientAddr);
        int clientFd = ::accept(m_serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientFd < 0) {
            if (m_running)
                Logger::warning("WebhookServer: accept() error: " + std::string(strerror(errno)));
            continue;
        }

        // Handle each connection in a detached thread for concurrency
        std::thread([this, clientFd] {
            handleClient(clientFd);
            ::close(clientFd);
        }).detach();
    }
}

void WebhookServer::handleClient(int fd) const {
    // ── Read request headers ──────────────────────────────────────────────
    std::string raw;
    raw.reserve(4096);
    char buf[1024];
    while (raw.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) return;
        buf[n] = '\0';
        raw.append(buf, static_cast<std::size_t>(n));
        if (raw.size() > 65536) return; // Reject oversized headers
    }

    // ── Parse request line ────────────────────────────────────────────────
    std::istringstream iss(raw);
    std::string method, requestPath, httpVer;
    iss >> method >> requestPath >> httpVer;

    if (method != "POST") {
        sendResponse(fd, 405, "Method Not Allowed");
        return;
    }

    // Strip query string from path for comparison
    auto pathBase = requestPath.substr(0, requestPath.find('?'));
    if (pathBase != m_path) {
        sendResponse(fd, 404, "Not Found");
        return;
    }

    // ── Parse Content-Length ──────────────────────────────────────────────
    std::size_t contentLength = 0;
    {
        const std::string cl_header = "content-length:";
        std::string lower = raw;
        for (auto& c : lower) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        auto pos = lower.find(cl_header);
        if (pos == std::string::npos) { sendResponse(fd, 411, "Length Required"); return; }
        contentLength = std::stoul(lower.substr(pos + cl_header.size()));
    }

    // ── Read body (may already be partially buffered after headers) ───────
    const auto headerEnd = raw.find("\r\n\r\n");
    std::string body     = raw.substr(headerEnd + 4);

    while (body.size() < contentLength) {
        ssize_t n = ::recv(fd, buf, std::min(sizeof(buf) - 1, contentLength - body.size()), 0);
        if (n <= 0) break;
        body.append(buf, static_cast<std::size_t>(n));
    }

    sendResponse(fd, 200, "OK");

    Logger::debug("WebhookServer: received update (" + std::to_string(body.size()) + " bytes)");
    m_callback(body);
}

std::string WebhookServer::readBody(int /*fd*/, std::size_t /*contentLength*/) {
    return {}; // Body reading is inlined in handleClient for simplicity
}

void WebhookServer::sendResponse(int fd, int status, const char* body) {
    const std::string resp =
        "HTTP/1.1 " + std::to_string(status) + " " + body + "\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(std::strlen(body)) + "\r\n"
        "Connection: close\r\n"
        "\r\n" + body;
    ::send(fd, resp.c_str(), resp.size(), 0);
}
