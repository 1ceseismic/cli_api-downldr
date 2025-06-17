#include "core/youtube_fetcher.h"
#include <iostream>
#include <fstream> // For debug file output
#include <regex>
#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <cpr/util.h> // For cpr::util::urlDecode

namespace yt_core {

// Helper function to extract YouTube video ID from various URL formats
static std::string extractVideoIdFromUrl(const std::string& videoUrl) {
    std::regex patterns[] = {
        std::regex(R"(v=([a-zA-Z0-9_-]{11}))"),
        std::regex(R"(youtu\.be\/([a-zA-Z0-9_-]{11}))"),
        std::regex(R"(embed\/([a-zA-Z0-9_-]{11}))"),
        std::regex(R"(shorts\/([a-zA-Z0-9_-]{11}))")
    };
    std::smatch match;
    for (const auto& pattern : patterns) {
        if (std::regex_search(videoUrl, match, pattern) && match.size() > 1) {
            return match[1].str();
        }
    }
    return "";
}

// Helper to safely get string from json
static std::string safeGetString(const nlohmann::json& j, const char* key) {
    if (j.contains(key) && j[key].is_string()) {
        return j[key].get<std::string>();
    }
    return "";
}

// Helper to safely get long from json
static long safeGetLong(const nlohmann::json& j, const char* key, long default_val = 0) {
    if (j.contains(key)) {
        if (j[key].is_number()) {
            return j[key].get<long>();
        } else if (j[key].is_string()) {
            try { return std::stol(j[key].get<std::string>()); } catch (const std::exception&) {}
        }
    }
    return default_val;
}

// Helper to safely get int from json
static int safeGetInt(const nlohmann::json& j, const char* key, int default_val = 0) {
    if (j.contains(key)) {
        if (j[key].is_number()) {
            return j[key].get<int>();
        } else if (j[key].is_string()) {
            try { return std::stoi(j[key].get<std::string>()); } catch (const std::exception&) {}
        }
    }
    return default_val;
}


YouTubeFetcher::YouTubeFetcher() {
    // std::cout << "YouTubeFetcher created." << std::endl;
}

YouTubeFetcher::~YouTubeFetcher() {
    // std::cout << "YouTubeFetcher destroyed." << std::endl;
}

std::optional<std::string> YouTubeFetcher::extractJsonFromHtml(const std::string& htmlContent) {
    size_t pos = htmlContent.find("ytInitialPlayerResponse = {");
    if (pos == std::string::npos) {
        pos = htmlContent.find("var ytInitialPlayerResponse = {");
        if (pos == std::string::npos) {
            // std::cerr << "Could not find 'ytInitialPlayerResponse = {' or 'var ytInitialPlayerResponse = {'." << std::endl;
            return std::nullopt;
        }
    }

    pos = htmlContent.find('{', pos);
    if (pos == std::string::npos) {
        // std::cerr << "Could not find start of ytInitialPlayerResponse JSON object after assignment." << std::endl;
        return std::nullopt;
    }

    int brace_count = 0;
    size_t start_pos = pos;
    size_t end_pos = std::string::npos;

    for (size_t i = start_pos; i < htmlContent.length(); ++i) {
        if (htmlContent[i] == '{') {
            brace_count++;
        } else if (htmlContent[i] == '}') {
            brace_count--;
            if (brace_count == 0) {
                end_pos = i;
                break;
            }
        }
    }

    if (end_pos == std::string::npos) {
        // std::cerr << "Could not find well-formed end of ytInitialPlayerResponse JSON object." << std::endl;
        return std::nullopt;
    }
    return htmlContent.substr(start_pos, end_pos - start_pos + 1);
}

void YouTubeFetcher::parseStreamFormats(const nlohmann::json& playerResponseJson, VideoDetails& details) {
    if (!playerResponseJson.contains("streamingData")) {
        // std::cout << "No 'streamingData' found in JSON." << std::endl;
        return;
    }
    const auto& streamingData = playerResponseJson["streamingData"];

    auto process_stream_array = [&](const nlohmann::json& stream_array, bool is_adaptive_array) {
        if (!stream_array.is_array()) return;

        for (const auto& item : stream_array) {
            if (!item.is_object()) continue;
            MediaStream stream;
            stream.itag = safeGetInt(item, "itag");
            stream.url = safeGetString(item, "url");

            if (stream.url.empty() && (item.contains("cipher") || item.contains("signatureCipher"))) {
                std::string cipher_val = safeGetString(item, "cipher");
                if(cipher_val.empty()) cipher_val = safeGetString(item, "signatureCipher");

                // Attempt to find a usable URL within the cipher/signatureCipher string.
                // This is a simplified approach. Full yt-dlp functionality involves
                // JavaScript execution for dynamic signature deciphering, which is not
                // implemented here. This code primarily looks for 'url=' parameter.
                // std::cout << "Stream itag " << stream.itag << " requires deciphering. Cipher: " << cipher_val.substr(0,30) << "..." << std::endl;
                std::regex url_regex("url=([^&]+)");
                std::smatch url_match;
                if (std::regex_search(cipher_val, url_match, url_regex) && url_match.size() > 1) {
                    stream.url = cpr::util::urlDecode(url_match[1].str());
                    // std::cout << "Found potential URL in cipher for itag " << stream.itag << std::endl;
                } else {
                    // std::cout << "Skipping stream with cipher for itag " << stream.itag << " (deciphering not implemented, no simple url found)." << std::endl;
                    continue; // Skip if no direct URL found and deciphering is needed
                }
            }
             if (stream.url.empty()){
                // std::cout << "Skipping stream itag " << stream.itag << " due to empty URL and no cipher." << std::endl;
                continue;
            }


            stream.mimeType = safeGetString(item, "mimeType");
            size_t codecs_pos = stream.mimeType.find("codecs=\"");
            if (codecs_pos != std::string::npos) {
                size_t start = codecs_pos + 8;
                size_t end = stream.mimeType.find('"', start);
                if (end != std::string::npos) {
                    stream.codecs = stream.mimeType.substr(start, end - start);
                }
            }

            stream.bitrate = safeGetLong(item, "bitrate");
            if (item.contains("contentLength") && item["contentLength"].is_string()){
                 stream.contentLength = std::stoll(item["contentLength"].get<std::string>());
            } else if (item.contains("contentLength") && item["contentLength"].is_number()){
                 stream.contentLength = item["contentLength"].get<long long>();
            } else if (item.contains("approxDurationMs") && item["approxDurationMs"].is_string() && stream.bitrate > 0) {
                // Estimate content length if not directly available, using duration and bitrate
                // This is an approximation.
                try {
                    long long durationMs = std::stoll(item["approxDurationMs"].get<std::string>());
                    stream.contentLength = (stream.bitrate / 8) * (durationMs / 1000);
                } catch (const std::exception&) { /* ignore error, contentLength remains nullopt */ }
            }


            if (item.contains("width")) stream.width = safeGetInt(item, "width");
            if (item.contains("height")) stream.height = safeGetInt(item, "height");
            if (item.contains("qualityLabel")) stream.qualityLabel = safeGetString(item, "qualityLabel");
            if (item.contains("fps")) stream.fps = safeGetInt(item, "fps");

            if (item.contains("audioQuality")) stream.audioQuality = safeGetString(item, "audioQuality");
            if (item.contains("audioSampleRate")) stream.audioSampleRate = safeGetLong(item, "audioSampleRate"); // Is string in JSON
            if (item.contains("audioChannels")) stream.audioChannels = safeGetInt(item, "audioChannels");

            stream.isDash = is_adaptive_array;
            if (is_adaptive_array) {
                if (stream.mimeType.find("audio/") != std::string::npos) {
                    stream.isAudioOnly = true; stream.isVideoOnly = false;
                } else if (stream.mimeType.find("video/") != std::string::npos) {
                    stream.isVideoOnly = true; stream.isAudioOnly = false;
                }
                details.adaptiveFormats.push_back(stream);
            } else {
                stream.isAudioOnly = true; stream.isVideoOnly = true;
                details.formats.push_back(stream);
            }
        }
    };

    if (streamingData.contains("formats")) {
        process_stream_array(streamingData["formats"], false);
    }
    if (streamingData.contains("adaptiveFormats")) {
        process_stream_array(streamingData["adaptiveFormats"], true);
    }
}

std::optional<VideoDetails> YouTubeFetcher::parseVideoDetailsJson(const nlohmann::json& jsonData, const std::string& videoId) {
    VideoDetails details;
    details.id = videoId;

    try {
        if (jsonData.contains("videoDetails") && jsonData["videoDetails"].is_object()) {
            const auto& vd = jsonData["videoDetails"];
            details.title = safeGetString(vd, "title");
            details.author = safeGetString(vd, "author");
            details.channelId = safeGetString(vd, "channelId");
            details.lengthSeconds = safeGetLong(vd, "lengthSeconds"); // Is string in JSON
            details.description = safeGetString(vd, "shortDescription");

            if (vd.contains("thumbnail") && vd["thumbnail"].is_object()) {
                const auto& tn = vd["thumbnail"];
                if (tn.contains("thumbnails") && tn["thumbnails"].is_array()) {
                    for (const auto& thumb : tn["thumbnails"]) {
                        if (thumb.contains("url") && thumb["url"].is_string()) {
                            details.thumbnails.push_back(thumb["url"].get<std::string>());
                        }
                    }
                }
            }
        } else {
            // std::cerr << "JSON does not contain 'videoDetails' object." << std::endl;
        }

        parseStreamFormats(jsonData, details);

        if (details.title.empty() && details.adaptiveFormats.empty() && details.formats.empty()) {
            // std::cerr << "Failed to parse essential video details or any formats." << std::endl;
        }

    } catch (const nlohmann::json::exception& e) {
        std::cerr << "JSON parsing error in parseVideoDetailsJson: " << e.what() << std::endl;
        return std::nullopt;
    }
    return details;
}

std::optional<VideoDetails> YouTubeFetcher::fetchVideoDetails(const std::string& videoUrl) {
    std::string videoId = extractVideoIdFromUrl(videoUrl);
    if (videoId.empty()) {
        std::cerr << "Could not extract video ID from URL: " << videoUrl << std::endl;
        return std::nullopt;
    }

    std::string watchUrl = "https://www.youtube.com/watch?v=" + videoId; // Removed &pbj=1 for now, rely on HTML scrape first

    cpr::Response r = cpr::Get(cpr::Url{watchUrl},
                               cpr::Header{{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"},
                                           {"Accept-Language", "en-US,en;q=0.9"}});
                                           // {"X-YouTube-Client-Name", "1"}, // Common headers used by web clients
                                           // {"X-YouTube-Client-Version", "2.20210721"}});


    if (r.status_code != 200) {
        std::cerr << "Failed to fetch URL: " << watchUrl << " Status code: " << r.status_code << std::endl;
        std::cerr << "Error: " << r.error.message << std::endl;
        return std::nullopt;
    }

    // Try to extract from HTML first, as it's the most common way yt-dlp works for initial data.
    auto json_str_opt = extractJsonFromHtml(r.text);
    if (!json_str_opt) {
        // std::cerr << "Failed to extract ytInitialPlayerResponse JSON from HTML content." << std::endl;
        // std::ofstream html_file("debug_youtube_page.html");
        // html_file << r.text;
        // html_file.close();
        // std::cerr << "Saved HTML to debug_youtube_page.html" << std::endl;

        // As a fallback, try fetching with pbj=1 if HTML scrape fails (e.g. different page structure)
        // std::cout << "HTML extraction failed, trying with &pbj=1" << std::endl;
        watchUrl = "https://www.youtube.com/watch?v=" + videoId + "&pbj=1";
        r = cpr::Get(cpr::Url{watchUrl},
                       cpr::Header{{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"},
                                   {"Accept-Language", "en-US,en;q=0.9"},
                                   {"X-YouTube-Client-Name", "1"},
                                   {"X-YouTube-Client-Version", "2.20210721"}});
        if (r.status_code == 200 && r.header["content-type"].find("application/json") != std::string::npos) {
            try {
                nlohmann::json jsonData = nlohmann::json::parse(r.text);
                if (jsonData.is_array()) {
                    for(const auto& element : jsonData) {
                        if (element.is_object() && element.contains("playerResponse")) {
                            return parseVideoDetailsJson(element["playerResponse"], videoId);
                        }
                        if (element.is_object() && element.contains("videoDetails") && element.contains("streamingData")) {
                             return parseVideoDetailsJson(element, videoId);
                        }
                    }
                } else if (jsonData.is_object() && jsonData.contains("playerResponse")) {
                    return parseVideoDetailsJson(jsonData["playerResponse"], videoId);
                } else if (jsonData.is_object() && jsonData.contains("videoDetails") && jsonData.contains("streamingData")) {
                     return parseVideoDetailsJson(jsonData, videoId);
                }
                // std::cerr << "Received JSON with pbj=1, but could not find playerResponse or videoDetails/streamingData." << std::endl;
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "JSON parsing error from pbj=1 response: " << e.what() << std::endl;
            }
        }
        return std::nullopt; // If HTML scrape and pbj=1 both fail
    }

    try {
        nlohmann::json jsonData = nlohmann::json::parse(json_str_opt.value());
        // std::ofstream json_file("debug_extracted_player_response.json");
        // json_file << jsonData.dump(2);
        // json_file.close();
        return parseVideoDetailsJson(jsonData, videoId);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parsing error from extracted string: " << e.what() << " at offset " << e.byte << std::endl;
        // std::ofstream problematic_json_file("debug_problematic_json.json");
        // problematic_json_file << json_str_opt.value();
        // problematic_json_file.close();
        return std::nullopt;
    }
}

// Implementation of downloadStream
bool YouTubeFetcher::downloadStream(const MediaStream& stream,
                                    const std::string& outputFilePath,
                                    ProgressCallback progressCallback) {
    if (stream.url.empty()) {
        std::cerr << "Error: Stream URL is empty. Cannot download." << std::endl;
        return false;
    }

    std::ofstream outputFile(outputFilePath, std::ios::binary);
    if (!outputFile.is_open()) {
        std::cerr << "Error: Could not open file for writing: " << outputFilePath << std::endl;
        return false;
    }

    long long totalBytesExpected = stream.contentLength.value_or(0);
    long long downloadedBytes = 0;

    // Use cpr::Session for more control and write callback
    cpr::Session session;
    session.SetUrl(cpr::Url{stream.url});
    session.SetHeader(cpr::Header{
        {"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"}
    });
    // YouTube might sometimes check for range requests or other headers for large files.
    // session.SetHeader(cpr::Header{{"Range", "bytes=0-"}}); // Example, may not be needed for all streams

    auto writeCallback = [&](std::string data, intptr_t /* SSDK_MAYBE_UNUSED user_data */) -> bool {
        outputFile.write(data.data(), data.length());
        if (!outputFile) {
            std::cerr << "Error writing to file." << std::endl;
            return false; // Stop download
        }
        downloadedBytes += data.length();
        if (progressCallback) {
            progressCallback(downloadedBytes, totalBytesExpected);
        }
        return true;
    };

    session.SetWriteCallback(cpr::WriteCallback{writeCallback});

    // Add timeout (optional but good practice)
    session.SetTimeout(cpr::Timeout{0}); // 0 for no timeout on transfer, but connect timeout still applies (default 5s)
                                         // Or set a specific long timeout like 3600s (1 hour)
    session.SetConnectTimeout(cpr::ConnectTimeout{10000}); // 10 seconds for connection

    cpr::Response response = session.Get();

    outputFile.close();

    if (response.error || response.status_code >= 400) {
        std::cerr << "Download failed." << std::endl;
        if (response.error) {
            std::cerr << "CPR Error: " << response.error.message << std::endl;
        }
        std::cerr << "Status code: " << response.status_code << std::endl;
        if (!response.text.empty() && response.text.length() < 500) { // Show small error bodies
             std::cerr << "Response body: " << response.text << std::endl;
        }
        // Attempt to delete partially downloaded file
        std::remove(outputFilePath.c_str());
        return false;
    }

    // Final progress update to ensure 100% is shown if totalBytesExpected was accurate
    if (progressCallback && totalBytesExpected > 0 && downloadedBytes == totalBytesExpected) {
        progressCallback(totalBytesExpected, totalBytesExpected);
    } else if (progressCallback && totalBytesExpected == 0 && downloadedBytes > 0) {
        // If total size was unknown, this signifies completion with actual downloaded size
        progressCallback(downloadedBytes, downloadedBytes);
    }


    return true;
}

} // namespace yt_core
