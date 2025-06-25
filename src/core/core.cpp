#include <iostream>
#include "core/youtube_fetcher.h" // Ensure it can be included

namespace yt_core {
    void initializeCore() {
        // std::cout << "yt_core initialized." << std::endl;
    }

    // Example function that might use YouTubeFetcher
    void testFetch(const std::string& url) {
        YouTubeFetcher fetcher;
        auto details = fetcher.fetchVideoDetails(url);
        if (details) {
            std::cout << "Fetched title: " << details->title << std::endl;
            std::cout << "Found " << details->adaptiveFormats.size() << " adaptive formats." << std::endl;
            std::cout << "Found " << details->formats.size() << " muxed formats." << std::endl;
        } else {
            std::cout << "Failed to fetch details for " << url << std::endl;
        }
    }
}
