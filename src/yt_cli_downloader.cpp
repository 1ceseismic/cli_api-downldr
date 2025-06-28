#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream> // For file operations
#include <algorithm> // For std::min
#include <regex> // For player URL extraction (no longer used for core logic but was here)
#include <memory> // For std::unique_ptr
#include <filesystem> // For std::filesystem::create_directories (C++17)
#include <cstdio> // For popen
#include <array>  // For reading command output
#include <cctype> // For isdigit
#include <cmath>  // For pow, log in format_bytes
#include <iomanip> // For setprecision, std::fixed for progress display
#include <chrono> // For download speed calculation

// HTTP and JSON libraries - Assuming these are available and discoverable by the build system
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define PROJECT_NAME "yt-cli-downloader"

// Helper function to format bytes into human-readable string (KB, MB, GB)
std::string format_bytes(long long bytes) {
    if (bytes == 0) {
        return "0 B";
    }
    const char* suffixes[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    int suffix_idx = 0;
    double d_bytes = static_cast<double>(bytes);

    if (bytes > 0) { // Avoid log(0) or log of negative
      suffix_idx = static_cast<int>(std::floor(std::log2(d_bytes) / 10.0)); // log2(1024) is 10
      if (suffix_idx < 0) suffix_idx = 0; // Should not happen for bytes > 0
      if (suffix_idx >= sizeof(suffixes) / sizeof(suffixes[0])) {
          suffix_idx = (sizeof(suffixes) / sizeof(suffixes[0])) - 1; // Use largest suffix
      }
      d_bytes /= std::pow(1024.0, suffix_idx);
    }


    char buffer[64];
    if (suffix_idx == 0) { // Bytes
        snprintf(buffer, sizeof(buffer), "%lld %s", bytes, suffixes[suffix_idx]);
    } else {
        snprintf(buffer, sizeof(buffer), "%.2f %s", d_bytes, suffixes[suffix_idx]);
    }
    return std::string(buffer);
}

// Function to execute a command and get its standard output
// Uses popen, which is POSIX-specific. For Windows, _popen or CreateProcess would be needed.
std::string execute_command_and_get_output(const std::string& command) {
    std::array<char, 128> buffer;
    std::string result;
    // std::cout << "Executing command: " << command << std::endl; // Make less verbose for version check

    std::string command_with_stderr = command + " 2>&1";

    FILE* pipe = popen(command_with_stderr.c_str(), "r");
    if (!pipe) {
        std::cerr << "popen() failed for command: " << command << std::endl;
        return "POPEN_FAILED";
    }
    try {
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            result += buffer.data();
        }
    } catch (...) {
        pclose(pipe);
        std::cerr << "Exception while reading pipe for command: " << command << std::endl;
        return "PIPE_READ_EXCEPTION";
    }

    int status = pclose(pipe);
    if (status == -1) {
        std::cerr << "pclose() failed for command: " << command << std::endl;
    } else {
        if (status != 0) {
             // Don't print error for version check if it's just a "not found" type error,
             // as check_ytdlp_availability will handle that.
             if (command.find("--version") == std::string::npos) { // Only print for non-version check commands
                std::cerr << "Command '" << command << "' exited with status " << status << "." << std::endl;
             }
             // If output contains error markers, it's an error regardless of exit status for yt-dlp -j
             if (result.find("ERROR:") != std::string::npos || result.find("Traceback") != std::string::npos) {
                // The result string now contains the error, return it.
             } else if (status != 0) {
                // For yt-dlp --version, a non-zero status with no clear error text might still be a problem
                // We'll rely on check_ytdlp_availability to interpret this for --version
                // For other commands, this is an issue.
                // We already return result, so if it's not JSON, parsing will fail.
             }
        }
    }
    // Trim trailing newline if present, common from command output
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

bool check_ytdlp_availability() {
    std::cout << "Checking for yt-dlp availability..." << std::endl;
    std::string command = "yt-dlp --version";
    std::string output = execute_command_and_get_output(command);

    if (output == "POPEN_FAILED") {
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        std::cerr << "ERROR: Failed to execute 'yt-dlp --version'." << std::endl;
        std::cerr << "This likely means 'yt-dlp' is not installed or not in your system's PATH." << std::endl;
        std::cerr << "Please install yt-dlp. See: https://github.com/yt-dlp/yt-dlp" << std::endl;
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        return false;
    }

    // Check for common command not found patterns (stderr is redirected to output)
    if (output.find("not recognized") != std::string::npos || // Windows "command not found"
        output.find("command not found") != std::string::npos || // POSIX "command not found"
        output.find("No such file or directory") != std::string::npos) {
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        std::cerr << "ERROR: 'yt-dlp' command not found." << std::endl;
        std::cerr << "Please ensure yt-dlp is installed and in your system's PATH." << std::endl;
        std::cerr << "Visit https://github.com/yt-dlp/yt-dlp for installation instructions." << std::endl;
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        return false;
    }

    if (output.find("ERROR:") != std::string::npos || output.find("Traceback") != std::string::npos) {
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        std::cerr << "ERROR: 'yt-dlp --version' command reported an error." << std::endl;
        std::cerr << "This could mean yt-dlp itself has an issue or its dependencies are missing." << std::endl;
        std::cerr << "Output was: " << output << std::endl;
        std::cerr << "Please check your yt-dlp installation." << std::endl;
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        return false;
    }

    bool looks_like_version = false;
    if (!output.empty() && isdigit(output[0])) { // yt-dlp versions usually start with year e.g. 2023.xx.xx
        if (output.find('.') != std::string::npos && output.length() > 5 && output.length() < 30) { // Basic sanity for version string
            looks_like_version = true;
        }
    }

    if (looks_like_version) {
        std::cout << "yt-dlp version found: " << output << std::endl;
        return true;
    } else {
         std::cout << "Warning: 'yt-dlp --version' returned an unexpected output: '" << output << "'" << std::endl;
         std::cout << "Attempting to proceed, but yt-dlp might not be functioning correctly." << std::endl;
         return true; // Proceed with caution
    }
}


// Helper to generate a somewhat safe filename
std::string sanitize_filename(const std::string& name) {
    std::string sanitized_name = name;
    std::string invalid_chars = "/\\:*?\"<>|";
    for (char& c : sanitized_name) {
        if (invalid_chars.find(c) != std::string::npos) {
            c = '_';
        }
    }
    return sanitized_name;
}

// Basic structure to hold video format information
struct VideoFormat {
    std::string itag;
    std::string quality;    // User-friendly quality string (e.g., "1080p", "720p60")
    std::string container;  // File extension (e.g., "mp4", "webm")
    std::string codecs;     // Combined audio/video codecs string
    std::string type;       // "video/audio", "video_only", "audio_only"
    std::string url;
    long long filesize = 0; // In bytes

    // Detailed video properties
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double vbr = 0.0; // Video bitrate in kbps
    double abr = 0.0; // Audio bitrate in kbps

    // Helper to check if format is video-only
    bool is_video_only() const { return type == "video_only"; }
    // Helper to check if format is audio-only
    bool is_audio_only() const { return type == "audio_only"; }
    // Helper to check if format has video component
    bool has_video() const { return type == "video/audio" || type == "video_only"; }
    // Helper to check if format has audio component
    bool has_audio() const { return type == "video/audio" || type == "audio_only"; }
};

// Basic structure to hold video metadata
struct VideoInfo {
    std::string id;
    std::string title;
    std::string author;
    long view_count;
    std::vector<VideoFormat> formats;
};

// Function to extract Video ID from various YouTube URL formats (kept for potential future use, not critical for yt-dlp)
std::string extract_video_id(const std::string& url) {
    std::string video_id_extracted; // Renamed to avoid conflict
    size_t pos = url.find("watch?v=");
    if (pos != std::string::npos) {
        video_id_extracted = url.substr(pos + 8);
        pos = video_id_extracted.find('&');
        if (pos != std::string::npos) {
            video_id_extracted = video_id_extracted.substr(0, pos);
        }
        return video_id_extracted;
    }

    pos = url.find("youtu.be/");
    if (pos != std::string::npos) {
        video_id_extracted = url.substr(pos + 9);
        pos = video_id_extracted.find('?');
        if (pos != std::string::npos) {
            video_id_extracted = video_id_extracted.substr(0, pos);
        }
        return video_id_extracted;
    }
    // std::cerr << "Warning: Could not extract video ID from URL: " << url << ". Assuming input is ID." << std::endl;
    return url;
}


// Function to fetch video info using yt-dlp
VideoInfo fetch_video_info(const std::string& video_url_or_id, const std::string& api_key /*unused*/) {
    VideoInfo info;
    info.id = video_url_or_id;

    // Construct the command. Pass video_url_or_id directly without extra quotes.
    // The shell invoked by popen will parse arguments. As long as video_url_or_id
    // doesn't contain spaces or special shell characters (typical for YT URLs/IDs),
    // it will be treated as a single argument to yt-dlp.
    std::string command = "yt-dlp -j --no-warnings --no-playlist " + video_url_or_id;
    std::cout << "Fetching video info using yt-dlp (this might take a moment)..." << std::endl;

    std::string json_output = execute_command_and_get_output(command);

    if (json_output.empty() || json_output == "POPEN_FAILED" || json_output == "PIPE_READ_EXCEPTION") {
        std::cerr << "Failed to execute yt-dlp or get output." << std::endl;
        if (json_output != "POPEN_FAILED" && json_output != "PIPE_READ_EXCEPTION" && !json_output.empty()) {
             std::cerr << "yt-dlp process output: " << json_output.substr(0, 500) << (json_output.length() > 500 ? "..." : "") << std::endl;
        }
        return info;
    }

    if (json_output.find("ERROR:") != std::string::npos ||
        json_output.find("Traceback (most recent call last):") != std::string::npos ||
        (json_output.find("is not a valid URL") != std::string::npos && json_output.find(video_url_or_id) != std::string::npos) ||
        json_output.find("Unsupported URL:") != std::string::npos) {
        std::cerr << "yt-dlp reported an error processing the video/URL:" << std::endl;
        std::cerr << json_output.substr(0, 1000) << (json_output.length() > 1000 ? "..." : "") << std::endl;
        return info;
    }

    try {
        json video_json = json::parse(json_output);

        if (video_json.contains("id") && video_json["id"].is_string()) {
            info.id = video_json["id"].get<std::string>();
        }
        if (video_json.contains("title") && video_json["title"].is_string()) {
            info.title = video_json["title"].get<std::string>();
        }
        if (video_json.contains("uploader") && video_json["uploader"].is_string()) {
            info.author = video_json["uploader"].get<std::string>();
        } else if (video_json.contains("channel") && video_json["channel"].is_string()) {
             info.author = video_json["channel"].get<std::string>();
        }
        if (video_json.contains("view_count") && video_json["view_count"].is_number()) {
            info.view_count = video_json["view_count"].get<long>();
        } else { info.view_count = 0; }


        if (video_json.contains("formats") && video_json["formats"].is_array()) {
            for (const auto& fmt_json : video_json["formats"]) {
                VideoFormat fmt;
                if (fmt_json.contains("format_id") && fmt_json["format_id"].is_string()) {
                    fmt.itag = fmt_json["format_id"].get<std::string>();
                } else { continue; }

                if (fmt_json.contains("url") && fmt_json["url"].is_string()) {
                    fmt.url = fmt_json["url"].get<std::string>();
                } else {
                    // std::cout << "Skipping format itag " << fmt.itag << " as it has no direct URL (likely a manifest)." << std::endl;
                    continue;
                }

                if (fmt_json.contains("protocol") && fmt_json["protocol"].is_string()){
                    std::string protocol = fmt_json["protocol"].get<std::string>();
                    if (protocol.find("m3u8") != std::string::npos || protocol.find("dash") != std::string::npos) {
                        // std::cout << "Skipping manifest format itag " << fmt.itag << " (protocol: " << protocol << ")" << std::endl;
                        continue;
                    }
                }
                if (fmt_json.contains("format") && fmt_json["format"].is_string() &&
                    fmt_json["format"].get<std::string>().find("storyboard") != std::string::npos) {
                    // std::cout << "Skipping storyboard format itag " << fmt.itag << std::endl;
                    continue;
                }

                if (fmt_json.contains("format_note") && fmt_json["format_note"].is_string()) {
                    fmt.quality = fmt_json["format_note"].get<std::string>();
                } else if (fmt_json.contains("resolution") && fmt_json["resolution"].is_string()) {
                     fmt.quality = fmt_json["resolution"].get<std::string>();
                } else if (fmt_json.contains("height") && fmt_json["height"].is_number()) {
                    fmt.quality = std::to_string(fmt_json["height"].get<int>()) + "p";
                }

                bool is_audio_only_from_vcodec = false;
                if (fmt_json.contains("vcodec") && fmt_json["vcodec"].is_string() && fmt_json["vcodec"].get<std::string>() == "none") {
                    is_audio_only_from_vcodec = true;
                }

                if (is_audio_only_from_vcodec && fmt_json.contains("abr") && fmt_json["abr"].is_number()) {
                     if (!fmt.quality.empty() && fmt.quality != "N/A" && fmt.quality.find("p") != std::string::npos) {
                     } else {
                        if (!fmt.quality.empty() && fmt.quality != "N/A") fmt.quality += ", ";
                        else fmt.quality.clear();
                        fmt.quality += std::to_string(static_cast<int>(fmt_json["abr"].get<double>())) + "kbps";
                     }
                }
                if (fmt.quality.empty()) fmt.quality = "N/A";

                if (fmt_json.contains("ext") && fmt_json["ext"].is_string()) {
                    fmt.container = fmt_json["ext"].get<std::string>();
                } else { fmt.container = "N/A"; }

                std::string vcodec = (fmt_json.contains("vcodec") && fmt_json["vcodec"].is_string()) ? fmt_json["vcodec"].get<std::string>() : "none";
                std::string acodec = (fmt_json.contains("acodec") && fmt_json["acodec"].is_string()) ? fmt_json["acodec"].get<std::string>() : "none";
                fmt.codecs = vcodec + " / " + acodec;

                bool has_video_stream = (vcodec != "none" && !vcodec.empty());
                bool has_audio_stream = (acodec != "none" && !acodec.empty());

                if (has_video_stream && has_audio_stream) { fmt.type = "video/audio"; }
                else if (has_video_stream) { fmt.type = "video_only"; }
                else if (has_audio_stream) { fmt.type = "audio_only"; }
                else { fmt.type = "unknown"; }

                // Populate filesize
                if (fmt_json.contains("filesize") && fmt_json["filesize"].is_number()) {
                    fmt.filesize = fmt_json["filesize"].get<long long>();
                } else if (fmt_json.contains("filesize_approx") && fmt_json["filesize_approx"].is_number()) {
                    fmt.filesize = fmt_json["filesize_approx"].get<long long>();
                } else {
                    fmt.filesize = 0;
                }

                // Populate detailed video properties
                if (fmt_json.contains("width") && fmt_json["width"].is_number()) {
                    fmt.width = fmt_json["width"].get<int>();
                }
                if (fmt_json.contains("height") && fmt_json["height"].is_number()) {
                    fmt.height = fmt_json["height"].get<int>();
                }
                if (fmt_json.contains("fps") && fmt_json["fps"].is_number()) {
                    fmt.fps = fmt_json["fps"].get<double>();
                }
                if (fmt_json.contains("vbr") && fmt_json["vbr"].is_number()) {
                    fmt.vbr = fmt_json["vbr"].get<double>();
                } else if (has_video_stream && !has_audio_stream && fmt_json.contains("tbr") && fmt_json["tbr"].is_number()) {
                    // Sometimes 'tbr' (total bitrate) is available for video-only streams and can be used as vbr
                    fmt.vbr = fmt_json["tbr"].get<double>();
                }
                if (fmt_json.contains("abr") && fmt_json["abr"].is_number()) {
                    fmt.abr = fmt_json["abr"].get<double>();
                } else if (has_audio_stream && !has_video_stream && fmt_json.contains("tbr") && fmt_json["tbr"].is_number()) {
                     // Sometimes 'tbr' (total bitrate) is available for audio-only streams and can be used as abr
                    fmt.abr = fmt_json["tbr"].get<double>();
                }


                // Refine quality string for video formats
                if (fmt.has_video()) {
                    std::string quality_str;
                    if (fmt.height > 0) {
                        quality_str += std::to_string(fmt.height) + "p";
                    }
                    if (fmt.fps > 0) {
                        // Only add fps if it's significantly different from standard (e.g. > 30 for typical videos, or if it's specified)
                        // This avoids clutter like "24p24"
                        if (fmt.fps > 30.0 || (fmt_json.contains("fps") && fmt_json["fps"].is_number()) ) {
                             // check if fps is whole number
                            if (static_cast<int>(fmt.fps) == fmt.fps) {
                                quality_str += std::to_string(static_cast<int>(fmt.fps));
                            } else {
                                std::ostringstream fps_ss;
                                fps_ss << std::fixed << std::setprecision(2) << fmt.fps;
                                std::string fps_str = fps_ss.str();
                                // Remove trailing zeros and decimal point if it's effectively an integer
                                fps_str.erase(fps_str.find_last_not_of('0') + 1, std::string::npos);
                                if (fps_str.back() == '.') fps_str.pop_back();
                                quality_str += fps_str;
                            }
                        }
                    }
                    if (!quality_str.empty()) {
                        fmt.quality = quality_str;
                    }
                } else if (fmt.is_audio_only() && fmt.abr > 0) {
                     fmt.quality = std::to_string(static_cast<int>(fmt.abr)) + "kbps";
                }


                info.formats.push_back(fmt);
            }
        }
        std::cout << "Successfully fetched and parsed video info using yt-dlp for: " << info.title << std::endl;

    } catch (const json::parse_error& e) {
        std::cerr << "Failed to parse JSON output from yt-dlp: " << e.what() << std::endl;
        std::cerr << "yt-dlp output (first 1000 chars): " << json_output.substr(0, 1000) << (json_output.length() > 1000 ? "..." : "") << std::endl;
        return info;
    }
    return info;
}

void display_video_info(const VideoInfo& info) {
    if (info.title.empty() && info.id.empty()) { // Check if ID is also empty (or still original input if title is empty)
        std::cout << "No video information to display (yt-dlp might have failed or video not found)." << std::endl;
        return;
    }
    std::cout << "\n--- Video Information ---" << std::endl;
    std::cout << "ID: " << info.id << std::endl;
    std::cout << "Title: " << info.title << std::endl;
    std::cout << "Author: " << info.author << std::endl;
    std::cout << "Views: " << info.view_count << std::endl;

    if (!info.formats.empty()) {
        std::cout << "\n--- Available Formats ---" << std::endl;
        for (const auto& fmt : info.formats) {
            std::cout << "Itag: " << std::left << std::setw(5) << fmt.itag
                      << " | Type: " << std::left << std::setw(12) << fmt.type
                      << " | Quality: " << std::left << std::setw(10) << fmt.quality;
            if (fmt.width > 0 && fmt.height > 0) {
                std::cout << " (" << fmt.width << "x" << fmt.height;
                if (fmt.fps > 0) {
                    std::cout << "@" << static_cast<int>(fmt.fps);
                }
                std::cout << ")";
            }
            std::cout << std::left << std::setw(5) << "" // Padding before next field
                      << " | Container: " << std::left << std::setw(7) << fmt.container
                      << " | Codecs: " << std::left << std::setw(20) << fmt.codecs;
            if (fmt.vbr > 0) {
                std::cout << " | VBR: " << std::fixed << std::setprecision(0) << fmt.vbr << "k";
            }
            if (fmt.abr > 0) {
                std::cout << " | ABR: " << std::fixed << std::setprecision(0) << fmt.abr << "k";
            }
            std::cout << " | Size: " << (fmt.filesize > 0 ? format_bytes(fmt.filesize) : "N/A")
                      << std::endl;
        }
    } else {
        std::cout << "No format information available (or yt-dlp found no suitable formats with URLs)." << std::endl;
    }
    std::cout << "-------------------------" << std::endl;
}

// Function to select best video and audio streams
struct SelectedStreams {
    const VideoFormat* video = nullptr;
    const VideoFormat* audio = nullptr;
    bool video_selected_by_tag = false; // True if video stream was selected by specific itag
    bool audio_selected_by_tag = false; // True if audio stream was selected by specific itag
    bool is_single_complete_stream = false; // True if the selection is a single, pre-muxed stream
};

SelectedStreams select_streams(const VideoInfo& info, const std::string& format_selection_str) {
    SelectedStreams result;
    const VideoFormat* best_video_only = nullptr;
    const VideoFormat* best_audio_only = nullptr;

    // Determine best available video-only stream
    for (const auto& fmt : info.formats) {
        if (fmt.is_video_only() && !fmt.url.empty()) {
            if (!best_video_only) {
                best_video_only = &fmt;
            } else {
                // Prioritize resolution (height), then FPS, then bitrate (vbr)
                if (fmt.height > best_video_only->height) best_video_only = &fmt;
                else if (fmt.height == best_video_only->height) {
                    if (fmt.fps > best_video_only->fps) best_video_only = &fmt;
                    else if (fmt.fps == best_video_only->fps) {
                        if (fmt.vbr > best_video_only->vbr) best_video_only = &fmt;
                    }
                }
            }
        }
    }

    // Determine best available audio-only stream
    for (const auto& fmt : info.formats) {
        if (fmt.is_audio_only() && !fmt.url.empty()) {
            if (!best_audio_only) {
                best_audio_only = &fmt;
            } else {
                // Prioritize bitrate (abr)
                if (fmt.abr > best_audio_only->abr) best_audio_only = &fmt;
            }
        }
    }

    if (format_selection_str.empty() || format_selection_str == "best") {
        result.video = best_video_only;
        result.audio = best_audio_only;
        if (!result.video && result.audio) {
             std::cout << "Info: No best video-only stream found. Selected best audio-only stream." << std::endl;
        } else if (result.video && !result.audio) {
             std::cout << "Info: No best audio-only stream found. Selected best video-only stream." << std::endl;
        } else if (!result.video && !result.audio) {
            std::cerr << "Warning: No suitable video-only or audio-only stream found for 'best' selection." << std::endl;
        }
        return result;
    }

    size_t plus_pos = format_selection_str.find('+');
    std::string video_part_str, audio_part_str;

    if (plus_pos != std::string::npos) { // Format like "itagV+itagA" or "bestvideo+itagA" etc.
        video_part_str = format_selection_str.substr(0, plus_pos);
        audio_part_str = format_selection_str.substr(plus_pos + 1);
    } else { // Single format specified: "itag", "bestvideo", or "bestaudio"
        if (format_selection_str == "bestvideo") {
            result.video = best_video_only;
            if (result.video) { // If best video is found, also try to get best audio
                result.audio = best_audio_only;
                if (!result.audio) std::cout << "Info: 'bestvideo' selected. Best video-only stream found, but no best audio-only stream to accompany it." << std::endl;
            } else {
                std::cerr << "Warning: No video-only stream found for 'bestvideo'." << std::endl;
            }
            return result;
        } else if (format_selection_str == "bestaudio") {
            result.audio = best_audio_only;
            if (!result.audio) std::cerr << "Warning: No audio-only stream found for 'bestaudio'." << std::endl;
            // If user *only* asks for bestaudio, we don't select a video.
            return result;
        } else { // Specific itag (e.g., "137", "140", "18")
            bool found = false;
            for(const auto& fmt : info.formats) {
                if (fmt.itag == format_selection_str) {
                    if (fmt.has_video() && fmt.has_audio()) { // Complete pre-muxed stream (e.g. itag '18')
                        result.video = &fmt;
                        result.audio = &fmt; // Video and Audio point to the same complete format
                        result.is_single_complete_stream = true;
                        std::cout << "Info: Selected itag '" << format_selection_str << "' is a complete video/audio stream." << std::endl;
                    } else if (fmt.is_video_only()) { // Video-only stream (e.g. itag '137')
                        result.video = &fmt;
                        result.video_selected_by_tag = true;
                        result.audio = best_audio_only; // Try to pair with best audio
                        if (!result.audio) std::cout << "Info: Video-only itag '" << format_selection_str << "' selected. No best audio-only stream found to accompany it." << std::endl;
                    } else if (fmt.is_audio_only()) { // Audio-only stream (e.g. itag '140')
                        result.audio = &fmt;
                        result.audio_selected_by_tag = true;
                        // If user explicitly asks for an audio-only itag, don't automatically add video.
                    } else {
                        std::cerr << "Warning: Selected itag '" << format_selection_str << "' is of unknown type or lacks a usable URL." << std::endl;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) std::cerr << "Error: Specified format itag '" << format_selection_str << "' not found." << std::endl;
            return result;
        }
    }

    // Handling "video_part_str + audio_part_str" (e.g., "137+140", "bestvideo+140")
    // Video part
    if (video_part_str == "bestvideo") {
        result.video = best_video_only;
        if (!result.video) std::cerr << "Warning: No video-only stream found for 'bestvideo' part of combined selection." << std::endl;
    } else if (!video_part_str.empty()) {
        bool found_v = false;
        for (const auto& fmt : info.formats) {
            if (fmt.itag == video_part_str) {
                if (fmt.has_video()) {
                    result.video = &fmt;
                    result.video_selected_by_tag = true;
                    // If this chosen video stream already has audio, and a *different* audio stream is also specified,
                    // the separate audio stream will take precedence. We don't need to warn here, merge logic handles it.
                } else {
                    std::cerr << "Warning: itag '" << video_part_str << "' (selected for video part) is not a video stream." << std::endl;
                }
                found_v = true;
                break;
            }
        }
        if (!found_v && !video_part_str.empty()) std::cerr << "Error: Video part itag '" << video_part_str << "' not found." << std::endl;
    }

    // Audio part
    if (audio_part_str == "bestaudio") {
        result.audio = best_audio_only;
        if (!result.audio) std::cerr << "Warning: No audio-only stream found for 'bestaudio' part of combined selection." << std::endl;
    } else if (!audio_part_str.empty()) {
        bool found_a = false;
        for (const auto& fmt : info.formats) {
            if (fmt.itag == audio_part_str) {
                if (fmt.has_audio()) {
                    result.audio = &fmt;
                    result.audio_selected_by_tag = true;
                    // If this chosen audio stream also has video, and a *different* video stream is also specified,
                    // the separate video stream will take precedence.
                } else {
                     std::cerr << "Warning: itag '" << audio_part_str << "' (selected for audio part) is not an audio stream." << std::endl;
                }
                found_a = true;
                break;
            }
        }
        if (!found_a && !audio_part_str.empty()) std::cerr << "Error: Audio part itag '" << audio_part_str << "' not found." << std::endl;
    }

    // If video and audio selections point to the exact same pre-muxed format (e.g. user entered "18+18")
    if (result.video && result.video == result.audio && result.video->has_video() && result.video->has_audio()) {
        result.is_single_complete_stream = true;
        std::cout << "Info: Video and audio selection point to the same complete stream (itag " << result.video->itag << ")." << std::endl;
    } else {
        // If they are different, or one is null, or one is not complete, it's not a single complete stream scenario for this flag.
        result.is_single_complete_stream = false;
    }

    // Final validation: if a component was specifically requested by itag, but it's not the right type.
    if (result.video_selected_by_tag && result.video && !result.video->has_video()){
        std::cerr << "Error: Explicitly selected video itag '" << result.video->itag << "' does not actually contain video." << std::endl;
        result.video = nullptr;
    }
    if (result.audio_selected_by_tag && result.audio && !result.audio->has_audio()){
        std::cerr << "Error: Explicitly selected audio itag '" << result.audio->itag << "' does not actually contain audio." << std::endl;
        result.audio = nullptr;
    }

    return result;
}

bool check_ffmpeg_availability() {
    std::cout << "Checking for ffmpeg availability..." << std::endl;
    std::string command = "ffmpeg -version";
    std::string output = execute_command_and_get_output(command);

    if (output == "POPEN_FAILED") {
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        std::cerr << "ERROR: Failed to execute 'ffmpeg -version'." << std::endl;
        std::cerr << "This likely means 'ffmpeg' is not installed or not in your system's PATH." << std::endl;
        std::cerr << "Please install ffmpeg: https://ffmpeg.org/download.html" << std::endl;
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        return false;
    }
    if (output.find("ffmpeg version") != std::string::npos || output.find("libavutil") != std::string::npos) { // Common strings in ffmpeg -version output
        std::cout << "ffmpeg found." << std::endl;
        // Further parsing of version could be done here if needed
        // For now, just confirming it runs and gives expected output is enough.
        return true;
    } else {
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        std::cerr << "ERROR: 'ffmpeg -version' returned unexpected output or ffmpeg not found." << std::endl;
        std::cerr << "Output was: " << output.substr(0, 200) << (output.length() > 200 ? "..." : "") << std::endl;
        std::cerr << "Please ensure ffmpeg is installed and in your system's PATH." << std::endl;
        std::cerr << "Visit https://ffmpeg.org/download.html for installation instructions." << std::endl;
        std::cerr << "--------------------------------------------------------------------" << std::endl;
        return false;
    }
}

// Function for downloading a single video/audio format stream
// Renamed from download_video_format to download_stream.
// `given_filename_suffix` is used to make temp file names like "title_video.mp4" or "title_audio.m4a" for merging.
// If `given_filename_suffix` is empty, it means it's a direct download of a format.
// The final filename will be "title.ext" if it's a complete stream, or "title_itag.ext" if it's a component.
bool download_stream(const VideoInfo& video_info, const VideoFormat& format_to_download, const std::string& given_filename_suffix, const std::string& output_dir = ".", std::string* out_downloaded_filepath = nullptr) {
    if (format_to_download.url.empty()) {
        std::cerr << "Error: Download URL for itag " << format_to_download.itag << " is empty." << std::endl;
        return false;
    }

    std::string base_filename = sanitize_filename(video_info.title.empty() ? video_info.id : video_info.title);

    std::string filename_extension = format_to_download.container;
    size_t semicolon_pos = filename_extension.find(';'); // Sanitize e.g. "mp4;"
    if (semicolon_pos != std::string::npos) {
        filename_extension = filename_extension.substr(0, semicolon_pos);
    }
    // Further sanitize and provide fallback for extension
    if (filename_extension.empty() || filename_extension == "N/A" || filename_extension.length() > 5 || filename_extension.find('.') != std::string::npos) {
        if (format_to_download.is_video_only()) filename_extension = "mkv"; // common for h264/vp9 in mkv
        else if (format_to_download.is_audio_only()) filename_extension = "m4a"; // common for aac
        else filename_extension = "mp4"; // Default for complete or unknown
    }

    std::string final_filename_part = base_filename;
    // Check if this is a direct download of a complete (muxed) format without a specific suffix.
    bool is_direct_complete_download = given_filename_suffix.empty() && format_to_download.has_video() && format_to_download.has_audio();

    if (!given_filename_suffix.empty()) {
        // Suffix is provided (e.g., "video", "audio" for temp files for merging)
        final_filename_part += "_" + given_filename_suffix;
    } else if (!is_direct_complete_download) {
        // No suffix given, AND it's NOT a complete stream (i.e., it's a component like video_only or audio_only being downloaded directly).
        // Append itag to distinguish. e.g. title_137.mkv (if 137 is video-only)
        final_filename_part += "_" + format_to_download.itag;
    }
    // If `is_direct_complete_download` is true (e.g. downloading format '18' which is complete, with no suffix),
    // filename becomes just `title.mp4` (no _itag, no suffix).

    std::string filename = output_dir + "/" + final_filename_part + "." + filename_extension;

    if (out_downloaded_filepath) {
        *out_downloaded_filepath = filename;
    }

    std::cout << "Attempting to download " << format_to_download.type << " (itag " << format_to_download.itag << ")"
              << " for video '" << video_info.title << "'"
              << " from URL: " << format_to_download.url.substr(0, 70) << (format_to_download.url.length() > 70 ? "..." : "")
              << " to " << filename << std::endl;

    try {
        if (!output_dir.empty() && output_dir != ".") {
            std::filesystem::path dir_path(output_dir);
            if (!std::filesystem::exists(dir_path)) {
                std::cout << "Creating output directory: " << output_dir << std::endl;
                if (!std::filesystem::create_directories(dir_path)) {
                    std::cerr << "Error: Could not create output directory: " << output_dir << std::endl;
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error while checking/creating output directory: " << e.what() << std::endl;
    }

    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open file for writing: " << filename << std::endl;
        return false;
    }

    // Progress reporting variables
    struct ProgressData {
        std::chrono::steady_clock::time_point last_update_time;
        std::chrono::steady_clock::time_point download_start_time;
        double last_downloaded_bytes = 0;
        long long total_bytes_to_download = 0;
        bool first_call = true;
    };
    ProgressData progress_data;
    progress_data.total_bytes_to_download = format_to_download.filesize;
    progress_data.download_start_time = std::chrono::steady_clock::now();
    progress_data.last_update_time = std::chrono::steady_clock::now();


    auto progress_callback = cpr::ProgressCallback(
        [&](cpr::cpr_off_t downloadTotal, cpr::cpr_off_t downloadNow,
            cpr::cpr_off_t /*uploadTotal*/, cpr::cpr_off_t /*uploadNow*/,
            intptr_t /*userdata*/) -> bool {

        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> time_since_last_update = current_time - progress_data.last_update_time;
        std::chrono::duration<double> time_since_start = current_time - progress_data.download_start_time;

        // Only update display roughly every 500ms or if download completed (downloadNow == downloadTotal)
        // or if it's the very first call to establish the baseline.
        // downloadTotal from CPR callback is often 0 if server doesn't send Content-Length, so use our known filesize.
        long long effective_total_bytes = progress_data.total_bytes_to_download > 0 ? progress_data.total_bytes_to_download : static_cast<long long>(downloadTotal);

        if (progress_data.first_call || time_since_last_update.count() >= 0.5 || (effective_total_bytes > 0 && downloadNow == effective_total_bytes)) {
            progress_data.first_call = false;
            double bytes_downloaded_since_last = static_cast<double>(downloadNow) - progress_data.last_downloaded_bytes;
            double current_speed_bps = 0;
            if (time_since_last_update.count() > 0.001) { // Avoid division by zero if calls are too rapid
                current_speed_bps = bytes_downloaded_since_last / time_since_last_update.count();
            }

            double average_speed_bps = 0;
            if (time_since_start.count() > 0.001) {
                 average_speed_bps = static_cast<double>(downloadNow) / time_since_start.count();
            }

            std::string eta_str = "ETA: N/A";
            if (effective_total_bytes > 0 && average_speed_bps > 0.001 && downloadNow < effective_total_bytes) {
                double remaining_bytes = static_cast<double>(effective_total_bytes - downloadNow);
                double eta_seconds = remaining_bytes / average_speed_bps;
                int h = static_cast<int>(eta_seconds / 3600);
                int m = static_cast<int>((eta_seconds - h * 3600) / 60);
                int s = static_cast<int>(eta_seconds - h * 3600 - m * 60);
                char eta_buffer[32];
                snprintf(eta_buffer, sizeof(eta_buffer), "ETA: %02d:%02d:%02d", h, m, s);
                eta_str = eta_buffer;
            } else if (effective_total_bytes > 0 && downloadNow >= effective_total_bytes) {
                eta_str = "ETA: Done";
            }


            std::cout << "\rProgress: ";
            if (effective_total_bytes > 0) {
                double percentage = (static_cast<double>(downloadNow) / effective_total_bytes) * 100.0;
                std::cout << std::fixed << std::setprecision(1) << percentage << "% | ";
            }
            std::cout << format_bytes(downloadNow)
                      << (effective_total_bytes > 0 ? " / " + format_bytes(effective_total_bytes) : "")
                      << " | Speed: " << format_bytes(static_cast<long long>(current_speed_bps)) << "/s"
                      << " | Avg Speed: " << format_bytes(static_cast<long long>(average_speed_bps)) << "/s"
                      << " | " << eta_str
                      << std::flush; // Flush to ensure \r works

            progress_data.last_downloaded_bytes = static_cast<double>(downloadNow);
            progress_data.last_update_time = current_time;
        }
        return true; // Continue download
    });

    cpr::Response r = cpr::Download(outfile, cpr::Url{format_to_download.url},
                                  cpr::Header{{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"}},
                                  progress_callback);

    std::cout << std::endl; // Newline after download finishes to clear progress line

    if (r.status_code >= 200 && r.status_code < 300 && r.error.code == cpr::ErrorCode::OK) {
        // std::cout << "Download completed successfully: " << filename << std::endl; // Already indicated by progress
        std::cout << "Final size: " << format_bytes(r.downloaded_bytes) << ". Saved to: " << filename << std::endl;
        return true;
    } else {
        std::cout << "Download failed." << std::endl; // Ensure this is on a new line
        std::cerr << "  Status code: " << r.status_code << std::endl;
        if (!r.error.message.empty()) {
             std::cerr << "  CPR Error: " << r.error.message << std::endl;
        }
        if (!r.status_line.empty()){
            std::cerr << "  Status line: " << r.status_line << std::endl;
        }
        if(!r.text.empty() && r.text.length() < 500) {
            std::cerr << "  Response body: " << r.text << std::endl;
        }
        outfile.close();
        if (std::remove(filename.c_str()) != 0) {
        } else {
            std::cout << "Partially downloaded file " << filename << " removed." << std::endl;
        }
        return false;
    }
}

// Function to download video and audio streams and merge them with ffmpeg
bool download_and_merge_streams(const VideoInfo& video_info, const VideoFormat& video_format, const VideoFormat& audio_format, const std::string& output_dir = ".", const std::string& final_filename_no_ext = "") {
    if (!check_ffmpeg_availability()) {
        std::cerr << "ffmpeg is required for merging streams but it's not available or not working." << std::endl;
        return false;
    }

    std::string video_filepath, audio_filepath;
    bool video_download_success = false;
    bool audio_download_success = false;

    std::cout << "\nDownloading video stream (itag " << video_format.itag << ")..." << std::endl;
    video_download_success = download_stream(video_info, video_format, "video_temp", output_dir, &video_filepath);

    if (!video_download_success) {
        std::cerr << "Failed to download video stream. Aborting merge." << std::endl;
        if (!video_filepath.empty()) std::remove(video_filepath.c_str()); // Clean up partial video file
        return false;
    }

    std::cout << "\nDownloading audio stream (itag " << audio_format.itag << ")..." << std::endl;
    audio_download_success = download_stream(video_info, audio_format, "audio_temp", output_dir, &audio_filepath);

    if (!audio_download_success) {
        std::cerr << "Failed to download audio stream. Aborting merge." << std::endl;
        if (!audio_filepath.empty()) std::remove(audio_filepath.c_str()); // Clean up partial audio file
        if (!video_filepath.empty()) std::remove(video_filepath.c_str()); // Clean up downloaded video file
        return false;
    }

    std::string sanitized_title = sanitize_filename(video_info.title.empty() ? video_info.id : video_info.title);
    std::string output_filename_base = final_filename_no_ext.empty() ? sanitized_title : sanitize_filename(final_filename_no_ext);

    // Determine a sensible output container, MKV is a good default.
    // Could also try to match video_format.container if it's mp4 and audio is compatible.
    std::string output_container = "mkv";
    // A more sophisticated approach might check codecs:
    // if (video_format.container == "mp4" && (audio_format.codecs.find("aac") != std::string::npos || audio_format.codecs.find("mp4a") != std::string::npos) ) {
    //    output_container = "mp4";
    // }

    std::string merged_filepath = output_dir + "/" + output_filename_base + "." + output_container;

    std::cout << "\nMerging video and audio streams using ffmpeg..." << std::endl;
    std::cout << "Video input: " << video_filepath << std::endl;
    std::cout << "Audio input: " << audio_filepath << std::endl;
    std::cout << "Output file: " << merged_filepath << std::endl;

    // Using -hide_banner for less verbose output, -loglevel error to only show critical errors.
    // Using \" for paths to handle spaces, though sanitize_filename should prevent most issues.
    std::string ffmpeg_command = "ffmpeg -hide_banner -loglevel error -y -i \"" + video_filepath + "\" -i \"" + audio_filepath + "\" -c copy \"" + merged_filepath + "\"";

    std::cout << "Executing ffmpeg command: " << ffmpeg_command << std::endl;
    std::string ffmpeg_output = execute_command_and_get_output(ffmpeg_command);

    bool merge_success = false;
    // Check if merged file exists and is not empty
    std::ifstream merged_file(merged_filepath, std::ios::binary | std::ios::ate);
    if (merged_file.good() && merged_file.tellg() > 0) {
        merge_success = true;
        std::cout << "Successfully merged streams to: " << merged_filepath << std::endl;
        std::cout << "Final size: " << format_bytes(merged_file.tellg()) << std::endl;
    } else {
        std::cerr << "ffmpeg merge failed or produced an empty file." << std::endl;
        if (!ffmpeg_output.empty() && ffmpeg_output != "POPEN_FAILED" && ffmpeg_output != "PIPE_READ_EXCEPTION") {
            std::cerr << "ffmpeg output:" << std::endl << ffmpeg_output << std::endl;
        }
         // Try to remove potentially corrupted merged file
        if (merged_file.is_open()) merged_file.close(); // Close before trying to remove
        std::remove(merged_filepath.c_str());
    }
    merged_file.close();

    // Clean up temporary files
    std::cout << "Cleaning up temporary files..." << std::endl;
    if (!video_filepath.empty() && std::remove(video_filepath.c_str()) == 0) {
        std::cout << "Removed temporary video file: " << video_filepath << std::endl;
    } else if (!video_filepath.empty()){
        std::cerr << "Warning: Failed to remove temporary video file: " << video_filepath << std::endl;
    }
    if (!audio_filepath.empty() && std::remove(audio_filepath.c_str()) == 0) {
        std::cout << "Removed temporary audio file: " << audio_filepath << std::endl;
    } else if (!audio_filepath.empty()){
        std::cerr << "Warning: Failed to remove temporary audio file: " << audio_filepath << std::endl;
    }

    return merge_success;
}



void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <video_url_or_id> [options]\n"
              << "Options:\n"
              << "  -h, --help                Show this help message\n"
              << "  -l, --list-formats        List available formats for the video\n"
              << "  -f, --format <format_str> Specify video/audio format string for download.\n"
              << "                            Examples: 'best', '137+140' (video_itag+audio_itag),\n"
              << "                            '18' (single pre-muxed itag), 'bestvideo', 'bestaudio'.\n"
              << "                            Default is 'best' (merged best quality).\n"
              << "  -o, --output <path>       Output directory or full filename template (e.g., \"./downloads/My Video.mkv\").\n"
              << "                            Defaults to current directory with video title as filename.\n"
              << "Requires yt-dlp and ffmpeg (for merging) to be installed and in PATH.\n";
}

int main(int argc, char **argv) {
    std::cout << PROJECT_NAME << " - YouTube CLI Downloader" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "This tool relies on 'yt-dlp' being installed and accessible in your system's PATH." << std::endl;

    if (!check_ytdlp_availability()) {
        return 1; // Exit if yt-dlp is not available
    }
    // ffmpeg check will be done only if merging is attempted.
    std::cout << "-------------------------------------------" << std::endl;


    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || (args.size() == 1 && (args[0] == "-h" || args[0] == "--help"))) {
        print_usage(argv[0]);
        return args.empty() ? 1 : 0;
    }

    std::string video_url_or_id_arg;
    std::string format_selection_str;
    std::string output_directory = ".";
    std::string output_filename_template;
    bool list_formats_flag = false;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-h" || args[i] == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (args[i] == "-l" || args[i] == "--list-formats") {
            list_formats_flag = true;
            // No need to break here, other arguments might be parsed for validity,
            // but list_formats_flag will take precedence later.
        }
        else if (args[i] == "-f" || args[i] == "--format") {
            if (i + 1 < args.size()) {
                format_selection_str = args[++i];
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (format string)." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        } else if (args[i] == "-o" || args[i] == "--output") {
            if (i + 1 < args.size()) {
                // Basic check: if the output path ends with a common video extension, assume it's a full filename template
                // Otherwise, assume it's a directory. This is a simplification.
                // A more robust solution would use std::filesystem::path and check if parent_path exists or if it has an extension.
                std::string potential_path = args[++i];
                if (potential_path.length() > 4 &&
                    (potential_path.substr(potential_path.length() - 4) == ".mp4" ||
                     potential_path.substr(potential_path.length() - 4) == ".mkv" ||
                     potential_path.substr(potential_path.length() - 4) == ".webm"))
                {
                    // Assume it's a filename template. Extract directory and filename.
                    std::filesystem::path p(potential_path);
                    output_directory = p.parent_path().string();
                    if (output_directory.empty() || output_directory == ".") {
                        // If parent path is current dir or empty, set to "." for consistency
                         output_directory = ".";
                    }
                    output_filename_template = p.stem().string(); // filename without extension
                } else {
                    output_directory = potential_path;
                }
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (directory or filename template)." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        }
         else if (video_url_or_id_arg.empty()) {
            video_url_or_id_arg = args[i];
        } else {
            std::cerr << "Error: Unknown argument or too many URLs/IDs: " << args[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (video_url_or_id_arg.empty()) {
        std::cerr << "Error: Video URL or ID is required." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    VideoInfo video_info = fetch_video_info(video_url_or_id_arg, "");
    if (video_info.title.empty() && (video_info.id == video_url_or_id_arg || video_info.id.empty()) ) {
        std::cerr << "Failed to fetch video info. Check if yt-dlp is installed and working, and if the video URL/ID is correct." << std::endl;
        return 1;
    }
    display_video_info(video_info);

    // If -l or --list-formats was used, display info (already done) and exit.
    if (list_formats_flag) {
        std::cout << "\nListing formats as requested. To download, omit -l and optionally use -f." << std::endl;
        return 0;
    }

    if (format_selection_str.empty()){
        std::cout << "\nNo format specified with -f. Defaulting to 'best' (best video + best audio merged)." << std::endl;
        format_selection_str = "best";
    }

    SelectedStreams streams = select_streams(video_info, format_selection_str);

    if (streams.is_single_complete_stream && streams.video) {
        // This means user selected a pre-muxed format (e.g. -f 18 or -f 18+18)
        // or selected a video-only/audio-only itag that was resolved to a complete stream by select_streams logic
        // (though select_streams tries to pair video-only with bestaudio).
        // More accurately, is_single_complete_stream is true if video and audio point to the *same* VideoFormat object
        // and that object has both video and audio.
        std::cout << "\nSelected format (itag " << streams.video->itag << ") is a complete stream." << std::endl;
        std::cout << "Attempting direct download..." << std::endl;
        if (download_stream(video_info, *streams.video, output_filename_template, output_directory)) { // Pass empty suffix for direct complete download
            std::cout << "Download of complete stream (itag " << streams.video->itag << ") finished." << std::endl;
        } else {
            std::cerr << "Download of complete stream (itag " << streams.video->itag << ") failed." << std::endl;
            return 1;
        }
    } else if (streams.video && streams.audio) {
        // We have separate video and audio streams to merge
        std::cout << "\nSelected video stream: itag " << streams.video->itag << " (" << streams.video->quality << ")" << std::endl;
        std::cout << "Selected audio stream: itag " << streams.audio->itag << " (" << streams.audio->quality << ")" << std::endl;
        std::cout << "Attempting to download and merge..." << std::endl;
        if (download_and_merge_streams(video_info, *streams.video, *streams.audio, output_directory, output_filename_template)) {
            std::cout << "Download and merge process completed." << std::endl;
        } else {
            std::cerr << "Download and merge process failed." << std::endl;
            return 1;
        }
    } else if (streams.video) { // Only video stream selected/available (and not a complete one)
        std::cout << "\nOnly a video stream was selected (itag " << streams.video->itag << ", type: " << streams.video->type << ")." << std::endl;
        std::cout << "Attempting to download video-only stream..." << std::endl;
        if (download_stream(video_info, *streams.video, output_filename_template, output_directory)) { // Empty suffix, will use itag if not complete
            std::cout << "Download of video-only stream (itag " << streams.video->itag << ") finished." << std::endl;
        } else {
            std::cerr << "Download of video-only stream (itag " << streams.video->itag << ") failed." << std::endl;
            return 1;
        }
    } else if (streams.audio) { // Only audio stream selected/available
        std::cout << "\nOnly an audio stream was selected (itag " << streams.audio->itag << ", type: " << streams.audio->type << ")." << std::endl;
        std::cout << "Attempting to download audio-only stream..." << std::endl;
        if (download_stream(video_info, *streams.audio, output_filename_template, output_directory)) { // Empty suffix, will use itag if not complete
            std::cout << "Download of audio-only stream (itag " << streams.audio->itag << ") finished." << std::endl;
        } else {
            std::cerr << "Download of audio-only stream (itag " << streams.audio->itag << ") failed." << std::endl;
            return 1;
        }
    } else {
        std::cerr << "\nNo suitable video or audio streams found for the selection '" << format_selection_str << "'." << std::endl;
        std::cout << "Please check available formats and your selection criteria." << std::endl;
        return 1;
    }

    return 0;
}
