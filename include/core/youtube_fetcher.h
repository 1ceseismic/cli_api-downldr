#ifndef YOUTUBE_FETCHER_H
#define YOUTUBE_FETCHER_H

#include "core/video_info.h" // Include the definitions from video_info.h
#include <string>
#include <optional>
#include <functional> // For std::function

// Forward declare nlohmann::json
namespace nlohmann { template<typename, typename, typename> class basic_json; using json = basic_json<void, void, std::string>; }

namespace yt_core {

class YouTubeFetcher {
public:
    YouTubeFetcher();
    ~YouTubeFetcher();

    std::optional<VideoDetails> fetchVideoDetails(const std::string& videoUrl);

    // Progress callback: current_bytes_downloaded, total_bytes_expected (can be 0 if unknown)
    using ProgressCallback = std::function<void(long long, long long)>;

    bool downloadStream(const MediaStream& stream,
                        const std::string& outputFilePath,
                        ProgressCallback progressCallback = nullptr); // Default to no callback

private:
    std::optional<std::string> extractJsonFromHtml(const std::string& htmlContent);
    std::optional<VideoDetails> parseVideoDetailsJson(const nlohmann::json& jsonData, const std::string& videoId);
    void parseStreamFormats(const nlohmann::json& playerResponseJson, VideoDetails& details);
};

// Function declarations for stream filtering and selection
std::vector<MediaStream> getAllStreams(const VideoDetails& details, bool adaptive_first = true);
std::vector<MediaStream> filterStreams(const std::vector<MediaStream>& streams, const FormatSelectionCriteria& criteria);
std::optional<MediaStream> selectBestStream(const std::vector<MediaStream>& streams, QualityPreference preference);

} // namespace yt_core

#endif // YOUTUBE_FETCHER_H
