#include "rssmanager.hpp"
#include "logger.hpp"
#include <pugixml.hpp>
#include <iostream>

RSSManager::RSSManager(Network& network) : m_network(network) {}

bool RSSManager::fetchFeed(const std::string& url, std::vector<RSSItem>& items, RSSFeed& feedInfo) {
    std::string response;
    if (!m_network.sendRequest(url, response, 30)) {
        Logger::error("RSSManager: Failed to fetch feed from " + url);
        return false;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(response.c_str());
    if (!result) {
        // Only if XML parsing fails, we check if it's actually an HTML response (likely a block or wrong URL)
        if (response.find("<html") != std::string::npos || response.find("<!DOCTYPE html") != std::string::npos) {
            Logger::error("RSSManager: URL returned an HTML page instead of XML for " + url + ". This is likely an anti-bot challenge (e.g., Cloudflare) or a homepage URL.");
        } else {
            Logger::error("RSSManager: Failed to parse XML from " + url + ": " + result.description() + ". Make sure this is a valid RSS feed.");
        }
        return false;
    }

    pugi::xml_node channel = doc.child("rss").child("channel");
    if (!channel) {
        Logger::error("RSSManager: Invalid RSS format (no channel node) for " + url);
        return false;
    }

    // Update feed info if empty
    if (feedInfo.title.empty()) feedInfo.title = channel.child_value("title");
    if (feedInfo.description.empty()) feedInfo.description = channel.child_value("description");

    for (pugi::xml_node item = channel.child("item"); item; item = item.next_sibling("item")) {
        RSSItem rssItem;
        rssItem.title = item.child_value("title");
        rssItem.link = item.child_value("link");
        rssItem.description = item.child_value("description");
        rssItem.guid = item.child_value("guid");
        if (rssItem.guid.empty()) rssItem.guid = rssItem.link; // Fallback to link as GUID
        rssItem.pubDate = item.child_value("pubDate");
        items.push_back(rssItem);
    }

    return true;
}
