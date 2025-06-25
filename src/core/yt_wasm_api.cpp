#include "core/yt_wasm_api.h"
#include "core/youtube_fetcher.h"
#include "core/video_info.h" // For VideoDetails and MediaStream
#include "nlohmann/json.hpp"
#include <string>
#include <vector>
#include <iostream> // For potential debug cout/cerr
#include <algorithm> // For std::find_if
#include <cstring>   // For std::strcpy and strdup
#include <cstdlib>   // For strdup and free
#include <sstream>   // For std::stringstream

// Static helper to convert MediaStream to JSON for get_filtered_video_info_json
// This version omits the URL.
static nlohmann::json stream_to_json(const yt_core::MediaStream& stream) {
    nlohmann::json j;
    j["itag"] = stream.itag;
    j["mimeType"] = stream.mimeType;
    j["codecs"] = stream.codecs; // MediaStream.codecs is std::string
    j["bitrate"] = stream.bitrate;
    if (stream.width.has_value()) j["width"] = stream.width.value(); else j["width"] = nullptr;
    if (stream.height.has_value()) j["height"] = stream.height.value(); else j["height"] = nullptr;
    if (stream.qualityLabel.has_value()) j["qualityLabel"] = stream.qualityLabel.value(); else j["qualityLabel"] = nullptr;
    if (stream.fps.has_value()) j["fps"] = stream.fps.value(); else j["fps"] = nullptr;
    if (stream.audioQuality.has_value()) j["audioQuality"] = stream.audioQuality.value(); else j["audioQuality"] = nullptr;
    if (stream.audioSampleRate.has_value()) j["audioSampleRate"] = stream.audioSampleRate.value(); else j["audioSampleRate"] = nullptr;
    if (stream.audioChannels.has_value()) j["audioChannels"] = stream.audioChannels.value(); else j["audioChannels"] = nullptr;
    if (stream.contentLength.has_value()) j["contentLength"] = stream.contentLength.value(); else j["contentLength"] = nullptr;
    j["isDash"] = stream.isDash;
    j["isAudioOnly"] = stream.isAudioOnly;
    j["isVideoOnly"] = stream.isVideoOnly;
    return j;
}

// Helper to convert MediaStream to JSON (global version, used by original get_video_info_json)
void to_json(nlohmann::json& j, const yt_core::MediaStream& stream) {
    j = nlohmann::json{
        {"itag", stream.itag},
        {"url", stream.url},
        {"mimeType", stream.mimeType}
    };
    j["codecs"] = stream.codecs; // stream.codecs is std::string, not optional

    j["bitrate"] = stream.bitrate;

    if (stream.width.has_value()) j["width"] = stream.width.value();
    else j["width"] = nullptr;
    if (stream.height.has_value()) j["height"] = stream.height.value();
    else j["height"] = nullptr;
    if (stream.qualityLabel.has_value()) j["qualityLabel"] = stream.qualityLabel.value();
    else j["qualityLabel"] = nullptr;
    if (stream.fps.has_value()) j["fps"] = stream.fps.value();
    else j["fps"] = nullptr;

    if (stream.audioQuality.has_value()) j["audioQuality"] = stream.audioQuality.value();
    else j["audioQuality"] = nullptr;
    if (stream.audioSampleRate.has_value()) j["audioSampleRate"] = stream.audioSampleRate.value();
    else j["audioSampleRate"] = nullptr;
    if (stream.audioChannels.has_value()) j["audioChannels"] = stream.audioChannels.value();
    else j["audioChannels"] = nullptr;

    if (stream.contentLength.has_value()) j["contentLength"] = stream.contentLength.value();
    else j["contentLength"] = nullptr;

    j["isDash"] = stream.isDash;
    j["isAudioOnly"] = stream.isAudioOnly;
    j["isVideoOnly"] = stream.isVideoOnly;
}

// Helper to convert VideoDetails to JSON
void to_json(nlohmann::json& j, const yt_core::VideoDetails& details) {
    j = nlohmann::json{
        {"id", details.id},
        {"title", details.title},
        {"author", details.author},
        {"channelId", details.channelId},
        {"lengthSeconds", details.lengthSeconds},
        {"description", details.description},
        {"thumbnails", details.thumbnails},
        {"formats", details.formats}, // Uses MediaStream to_json
        {"adaptiveFormats", details.adaptiveFormats} // Uses MediaStream to_json
    };
}


// Ensure C linkage for the implementations
extern "C" {

const char* get_video_info_json(const char* video_url_c_str) {
    if (!video_url_c_str) {
        nlohmann::json err_json = {
            {"success", false},
            {"error", "Null URL provided."}
        };
        return strdup(err_json.dump().c_str());
    }

    std::string video_url(video_url_c_str);
    yt_core::YouTubeFetcher fetcher;
    std::optional<yt_core::VideoDetails> details_opt;

    try {
        details_opt = fetcher.fetchVideoDetails(video_url);
    } catch (const std::exception& e) {
        // Catch potential exceptions from within fetchVideoDetails (e.g. bad_alloc, or from CPR/JSON libs if not caught internally)
        nlohmann::json err_json = {
            {"success", false},
            {"error", "C++ exception during fetchVideoDetails: " + std::string(e.what())}
        };
        return strdup(err_json.dump().c_str());
    } catch (...) {
        // Catch any other unknown exceptions
        nlohmann::json err_json = {
            {"success", false},
            {"error", "Unknown C++ exception during fetchVideoDetails."}
        };
        return strdup(err_json.dump().c_str());
    }


    nlohmann::json result_json;
    if (details_opt) {
        result_json["success"] = true;
        // Use the custom to_json for VideoDetails
        nlohmann::json data_json;
        to_json(data_json, details_opt.value()); // This serializes VideoDetails with our custom logic
        result_json["data"] = data_json;
    } else {
        result_json["success"] = false;
        result_json["error"] = "Failed to fetch video details. The URL might be invalid, private, or a network error occurred.";
    }

    return strdup(result_json.dump().c_str());
}

void free_c_string(char* str_ptr) {
    if (str_ptr) {
        std::free(str_ptr); // Use std::free for memory allocated by strdup
    }
}

// Filter string parser for WASM API
static yt_core::FormatSelectionCriteria parse_wasm_filter_string(const std::string& filter_str) {
    yt_core::FormatSelectionCriteria criteria; // Defaults are set in struct definition

    if (filter_str.empty()) {
        return criteria;
    }

    std::stringstream ss_filter(filter_str);
    std::string segment;

    while(std::getline(ss_filter, segment, ',')) {
        std::stringstream ss_segment(segment);
        std::string key, value;
        if (std::getline(ss_segment, key, ':') && std::getline(ss_segment, value)) {
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));
            key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
            value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
            value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);

            if (key == "res") {
                if (value == "best") {
                    criteria.quality_preference = yt_core::QualityPreference::BEST_RESOLUTION;
                    if (criteria.stream_type == yt_core::StreamTypePreference::ANY) criteria.stream_type = yt_core::StreamTypePreference::VIDEO_ONLY;
                } else if (value == "worst") {
                    criteria.quality_preference = yt_core::QualityPreference::WORST_RESOLUTION;
                    if (criteria.stream_type == yt_core::StreamTypePreference::ANY) criteria.stream_type = yt_core::StreamTypePreference::VIDEO_ONLY;
                } else {
                    try { criteria.target_height = std::stoi(value); } catch (const std::exception&) { /* ignore malformed */ }
                }
            } else if (key == "bitrate") {
                if (value == "best") criteria.quality_preference = yt_core::QualityPreference::BEST_BITRATE;
                else if (value == "worst") criteria.quality_preference = yt_core::QualityPreference::WORST_BITRATE;
            } else if (key == "audio_br" || key == "abr") {
                if (value == "best") {
                    criteria.quality_preference = yt_core::QualityPreference::BEST_AUDIO_BITRATE;
                    criteria.stream_type = yt_core::StreamTypePreference::AUDIO_ONLY;
                } else if (value == "worst") {
                    criteria.quality_preference = yt_core::QualityPreference::WORST_AUDIO_BITRATE;
                    criteria.stream_type = yt_core::StreamTypePreference::AUDIO_ONLY;
                }
            } else if (key == "type") {
                if (value == "video") criteria.stream_type = yt_core::StreamTypePreference::VIDEO_ONLY;
                else if (value == "audio") criteria.stream_type = yt_core::StreamTypePreference::AUDIO_ONLY;
                else if (value == "muxed") {
                    criteria.stream_type = yt_core::StreamTypePreference::MUXED;
                    criteria.prefer_adaptive_over_muxed = false;
                }
            } else if (key == "fps") {
                try { criteria.target_fps = std::stoi(value); } catch (const std::exception&) { /* ignore malformed */ }
            } else if (key == "vcodec") {
                criteria.preferred_codec_video = value;
                 if (criteria.stream_type == yt_core::StreamTypePreference::ANY) criteria.stream_type = yt_core::StreamTypePreference::VIDEO_ONLY;
            } else if (key == "acodec") {
                criteria.preferred_codec_audio = value;
                if (criteria.stream_type == yt_core::StreamTypePreference::ANY) criteria.stream_type = yt_core::StreamTypePreference::AUDIO_ONLY;
            }
        }
    }
    return criteria;
}

EMSCRIPTEN_KEEPALIVE
const char* get_filtered_video_info_json(const char* video_url_c_str, const char* filter_criteria_c_str) {
    std::string video_url = video_url_c_str ? video_url_c_str : "";
    std::string filter_str = filter_criteria_c_str ? filter_criteria_c_str : "";
    nlohmann::json response_json;

    if (video_url.empty()) {
        response_json["success"] = false;
        response_json["error"] = "Video URL is required.";
        return strdup(response_json.dump().c_str());
    }

    yt_core::YouTubeFetcher fetcher;
    yt_core::FormatSelectionCriteria criteria; // Default criteria

    if (!filter_str.empty()) {
        criteria = parse_wasm_filter_string(filter_str);
    }

    auto video_details_opt = fetcher.fetchVideoDetails(video_url);

    if (!video_details_opt) {
        response_json["success"] = false;
        response_json["error"] = "Failed to fetch video details from YouTube.";
        return strdup(response_json.dump().c_str());
    }

    yt_core::VideoDetails details = video_details_opt.value();

    if (!filter_str.empty()) {
        std::vector<yt_core::MediaStream> all_streams = yt_core::getAllStreams(details, criteria.prefer_adaptive_over_muxed);
        std::vector<yt_core::MediaStream> filtered_streams = yt_core::filterStreams(all_streams, criteria);

        if (criteria.quality_preference != yt_core::QualityPreference::NONE && !filtered_streams.empty()) {
            auto best_opt = yt_core::selectBestStream(filtered_streams, criteria.quality_preference);
            filtered_streams = best_opt ? std::vector<yt_core::MediaStream>{best_opt.value()} : std::vector<yt_core::MediaStream>{};
        }

        details.formats.clear();
        details.adaptiveFormats.clear();
        for (const auto& stream : filtered_streams) {
            if (stream.isDash) {
                details.adaptiveFormats.push_back(stream);
            } else {
                details.formats.push_back(stream);
            }
        }
    }

    try {
        nlohmann::json data_json;
        data_json["id"] = details.id;
        data_json["title"] = details.title;
        data_json["author"] = details.author;
        data_json["channelId"] = details.channelId;
        data_json["lengthSeconds"] = details.lengthSeconds;
        data_json["description"] = details.description;
        data_json["thumbnails"] = details.thumbnails;

        nlohmann::json formats_json_array = nlohmann::json::array();
        for (const auto& fmt : details.formats) {
            formats_json_array.push_back(stream_to_json(fmt));
        }
        data_json["formats"] = formats_json_array;

        nlohmann::json adaptive_formats_json_array = nlohmann::json::array();
        for (const auto& fmt : details.adaptiveFormats) {
            adaptive_formats_json_array.push_back(stream_to_json(fmt));
        }
        data_json["adaptiveFormats"] = adaptive_formats_json_array;

        response_json["success"] = true;
        response_json["data"] = data_json;

    } catch (const std::exception& e) {
        response_json["success"] = false;
        response_json["error"] = "JSON serialization failed: " + std::string(e.what());
    }

    return strdup(response_json.dump().c_str());
}

} // extern "C"


// Simplified Filename Sanitization and Extension logic for Wasm API
// (These could be more robust or shared if refactored from CLI's main.cpp)
static std::string sanitizeFilenameForWasm(const std::string& input, size_t maxLength = 100) {
    std::string output = input;
    std::replace_if(output.begin(), output.end(), [](char c){
        return std::string("<>:\"/\\|?*").find(c) != std::string::npos || (unsigned char)c < 32;
    }, '_');
    output.erase(0, output.find_first_not_of(" \t\n\r\f\v."));
    output.erase(output.find_last_not_of(" \t\n\r\f\v.") + 1);
    if (output.length() > maxLength) {
        output = output.substr(0, maxLength);
        output.erase(output.find_last_not_of(" \t\n\r\f\v.") + 1);
    }
    return output.empty() ? "download" : output;
}

static std::string getExtensionFromMimeTypeForWasm(const std::string& mimeType) {
    if (mimeType.find("video/mp4") != std::string::npos) return ".mp4";
    if (mimeType.find("video/webm") != std::string::npos) return ".webm";
    if (mimeType.find("audio/mp4") != std::string::npos) return ".m4a";
    if (mimeType.find("audio/webm") != std::string::npos) return ".webm";
    if (mimeType.find("audio/mpeg") != std::string::npos) return ".mp3";
    return ".bin";
}


extern "C" {

// get_video_info_json and free_c_string implementations are above this part in the actual diff

const char* get_stream_url_json(const char* video_url_c_str, int itag) {
    if (!video_url_c_str) {
        nlohmann::json err_json = {{"success", false}, {"error", "Null URL provided."}};
        return strdup(err_json.dump().c_str());
    }

    std::string video_url(video_url_c_str);
    yt_core::YouTubeFetcher fetcher;
    nlohmann::json result_json;

    try {
        std::optional<yt_core::VideoDetails> details_opt = fetcher.fetchVideoDetails(video_url);
        if (!details_opt) {
            throw std::runtime_error("Failed to fetch video details.");
        }

        const auto& details = details_opt.value();
        const yt_core::MediaStream* selected_stream_ptr = nullptr;

        auto find_by_itag = [&](const yt_core::MediaStream& s){ return s.itag == itag; };

        auto it_format = std::find_if(details.formats.begin(), details.formats.end(), find_by_itag);
        if (it_format != details.formats.end()) {
            selected_stream_ptr = &(*it_format);
        } else {
            auto it_adaptive = std::find_if(details.adaptiveFormats.begin(), details.adaptiveFormats.end(), find_by_itag);
            if (it_adaptive != details.adaptiveFormats.end()) {
                selected_stream_ptr = &(*it_adaptive);
            }
        }

        if (selected_stream_ptr && !selected_stream_ptr->url.empty()) {
            std::string quality_label_str = "itag" + std::to_string(selected_stream_ptr->itag);
            if(selected_stream_ptr->qualityLabel.has_value() && !selected_stream_ptr->qualityLabel.value().empty()) { // Check has_value and not empty
                quality_label_str = selected_stream_ptr->qualityLabel.value();
            } else if (selected_stream_ptr->height.has_value()) {
                quality_label_str = std::to_string(selected_stream_ptr->height.value()) + "p";
            } else if (selected_stream_ptr->isAudioOnly && selected_stream_ptr->audioQuality.has_value() && !selected_stream_ptr->audioQuality.value().empty()) { // Check has_value and not empty
                quality_label_str = selected_stream_ptr->audioQuality.value();
            }


            std::string suggested_filename =
                sanitizeFilenameForWasm(details.title, 60) + "_" +
                sanitizeFilenameForWasm(quality_label_str, 30) + // Increased length for quality label part
                getExtensionFromMimeTypeForWasm(selected_stream_ptr->mimeType);

            result_json["success"] = true;
            result_json["url"] = selected_stream_ptr->url;
            result_json["suggested_filename"] = suggested_filename;
        } else {
            throw std::runtime_error("Stream with specified itag not found or has no URL.");
        }

    } catch (const std::exception& e) {
        result_json["success"] = false;
        result_json["error"] = std::string("Error getting stream URL: ") + e.what();
    }

    return strdup(result_json.dump().c_str());
}

} // extern "C"
