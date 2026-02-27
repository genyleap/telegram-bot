#ifndef RSSMANAGER_HPP
#define RSSMANAGER_HPP

#include <string>
#include <vector>
#include <optional>
#include "persistence.hpp"
#include "network.hpp"

struct RSSItem {
    std::string title;
    std::string link;
    std::string description;
    std::string guid;
    std::string pubDate;
};

class RSSManager {
public:
    explicit RSSManager(Network& network);

    /**
     * @brief Fetches and parses an RSS feed.
     * @param url The URL of the RSS feed.
     * @param items Vector to store the parsed items.
     * @param feedInfo Reference to update feed info (title, description).
     * @return true if successful, false otherwise.
     */
    bool fetchFeed(const std::string& url, std::vector<RSSItem>& items, RSSFeed& feedInfo);

private:
    Network& m_network;
};

#endif // RSSMANAGER_HPP
