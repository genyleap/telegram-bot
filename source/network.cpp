#include "network.hpp"
#include "logger.hpp"
#include <format>

Network::Network() : m_curlHandle(curl_easy_init()) {
    if (!m_curlHandle) {
        Logger::error("Failed to initialize CURL handle in Network constructor.");
    }
}

Network::~Network() {
    if (m_curlHandle) {
        curl_easy_cleanup(m_curlHandle);
    }
}

size_t Network::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* outBuffer) {
    size_t totalSize = size * nmemb;
    outBuffer->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

int Network::ProgressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    if (clientp) {
        auto* cancelCheck = static_cast<std::function<bool()>*>(clientp);
        if (cancelCheck && (*cancelCheck) && (*cancelCheck)()) {
            return 1; // Non-zero value aborts the transfer
        }
    }
    return 0;
}

std::string Network::urlEncode(const std::string& text) const {
    if (!m_curlHandle) return text;
    
    std::lock_guard<std::mutex> lock(m_networkMutex);
    char* encoded = curl_easy_escape(m_curlHandle, text.c_str(), static_cast<int>(text.length()));
    if (encoded) {
        std::string result(encoded);
        curl_free(encoded);
        return result;
    }
    return text;
}

std::string Network::buildQueryString(const std::map<std::string, std::string>& params) const {
    std::string queryString;
    for (const auto& [key, value] : params) {
        queryString += std::format("{}={}&", key, urlEncode(value));
    }
    if (!queryString.empty()) queryString.pop_back(); // Remove trailing '&'
    return queryString;
}

bool Network::sendRequest(const std::string& url, std::string& response,
                          long timeoutSeconds, bool verbose,
                          std::function<bool()> cancelCheck,
                          const std::map<std::string, std::string>& headers) {
    if (!m_curlHandle) {
        Logger::error("CURL handle not initialized.");
        return false;
    }

    std::lock_guard<std::mutex> lock(m_networkMutex);
    
    curl_easy_reset(m_curlHandle);
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &response);

    // Set headers if provided
    struct curl_slist* chunk = nullptr;
    for (const auto& [key, value] : headers) {
        chunk = curl_slist_append(chunk, std::format("{}: {}", key, value).c_str());
    }
    if (chunk) {
        curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, chunk);
    }

    if (timeoutSeconds > 0)
        curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT, timeoutSeconds);
    
    // Modern best practices
    curl_easy_setopt(m_curlHandle, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(m_curlHandle, CURLOPT_FOLLOWLOCATION, 1L);
    // Set dynamic User-Agent based on Host OS to avoid anti-bot blocks
#if defined(_WIN32)
    const char* userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";
#elif defined(__APPLE__)
    const char* userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";
#elif defined(__linux__)
    const char* userAgent = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";
#else
    const char* userAgent = "Mozilla/5.0 (compatible; TelegramBot/1.0)";
#endif
    curl_easy_setopt(m_curlHandle, CURLOPT_USERAGENT, userAgent);

    if (verbose)
        curl_easy_setopt(m_curlHandle, CURLOPT_VERBOSE, 1L);

    // SSL Robustness
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Use modern TLS versions
    curl_easy_setopt(m_curlHandle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    if (cancelCheck) {
        curl_easy_setopt(m_curlHandle, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        curl_easy_setopt(m_curlHandle, CURLOPT_XFERINFODATA, &cancelCheck);
        curl_easy_setopt(m_curlHandle, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode res = curl_easy_perform(m_curlHandle);

    if (chunk) {
        curl_slist_free_all(chunk);
    }

    if (res != CURLE_OK) {
        if (res != CURLE_ABORTED_BY_CALLBACK) {
            Logger::formattedError("CURL error: {}", curl_easy_strerror(res));
        }
        return false;
    }

    return true;
}

bool Network::postRequest(const std::string& url, const std::string& body, std::string& response,
                          long timeoutSeconds, bool verbose,
                          const std::map<std::string, std::string>& headers) {
    if (!m_curlHandle) return false;

    std::lock_guard<std::mutex> lock(m_networkMutex);
    curl_easy_reset(m_curlHandle);
    curl_easy_setopt(m_curlHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_POST, 1L);
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(m_curlHandle, CURLOPT_POSTFIELDSIZE, body.length());
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(m_curlHandle, CURLOPT_WRITEDATA, &response);
    if (timeoutSeconds > 0) curl_easy_setopt(m_curlHandle, CURLOPT_TIMEOUT, timeoutSeconds);

    struct curl_slist* chunk = nullptr;
    for (const auto& [key, value] : headers) {
        chunk = curl_slist_append(chunk, std::format("{}: {}", key, value).c_str());
    }
    if (chunk) curl_easy_setopt(m_curlHandle, CURLOPT_HTTPHEADER, chunk);

    CURLcode res = curl_easy_perform(m_curlHandle);
    if (chunk) curl_slist_free_all(chunk);

    if (res != CURLE_OK) {
        Logger::formattedError("CURL POST error: {}", curl_easy_strerror(res));
        return false;
    }
    return true;
}
