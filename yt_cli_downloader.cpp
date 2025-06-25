#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream> // For file operations
#include <algorithm> // For std::min

// HTTP and JSON libraries - Assuming these are available and discoverable by the build system
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#define PROJECT_NAME "yt-cli-downloader"

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
    std::string quality;
    std::string container;
    std::string codecs;
    std::string type; // "video", "audio", "video_only", "audio_only"
    std::string url; // Actual download URL (may need separate extraction)
};

// Basic structure to hold video metadata
struct VideoInfo {
    std::string id;
    std::string title;
    std::string author;
    long view_count;
    std::vector<VideoFormat> formats;
    // Add more fields as needed, e.g., description, duration, thumbnails
};

// Function to extract Video ID from various YouTube URL formats
std::string extract_video_id(const std::string& url) {
    std::string video_id;
    // Example: https://www.youtube.com/watch?v=VIDEO_ID
    size_t pos = url.find("watch?v=");
    if (pos != std::string::npos) {
        video_id = url.substr(pos + 8);
        pos = video_id.find('&'); // Remove extra parameters
        if (pos != std::string::npos) {
            video_id = video_id.substr(0, pos);
        }
        return video_id;
    }

    // Example: https://youtu.be/VIDEO_ID
    pos = url.find("youtu.be/");
    if (pos != std::string::npos) {
        video_id = url.substr(pos + 9);
        pos = video_id.find('?'); // Remove extra parameters
        if (pos != std::string::npos) {
            video_id = video_id.substr(0, pos);
        }
        return video_id;
    }
    // Add more patterns if needed (e.g., /embed/, /shorts/)
    std::cerr << "Warning: Could not extract video ID from URL: " << url << std::endl;
    return ""; // Return empty if no standard pattern matches
}


// Function to fetch video info.
// Tries keyless method (web page scraping) first.
// API key is now optional and could be used for a fallback or specific features later.
VideoInfo fetch_video_info(const std::string& video_id, const std::string& api_key = "") {
    VideoInfo info;
    info.id = video_id;
    bool scraping_successful = false;

    std::cout << "Attempting to fetch video info for ID: " << video_id << " using web scraping." << std::endl;

    std::string watch_url = "https://www.youtube.com/watch?v=" + video_id;
    cpr::Response r = cpr::Get(cpr::Url{watch_url},
                               cpr::Header{{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"}});

    if (r.status_code == 200) {
        std::string html_content = r.text;
        std::string json_str;

        // Try to find "var ytInitialPlayerResponse = {"
        size_t start_pos = html_content.find("var ytInitialPlayerResponse = {");
        if (start_pos == std::string::npos) {
            // Try "ytInitialPlayerResponse = {" (common in some layouts)
            start_pos = html_content.find("ytInitialPlayerResponse = {");
            if (start_pos != std::string::npos) {
                 start_pos += std::string("ytInitialPlayerResponse = ").length() -1; // Get to the '{'
            }
        } else {
            start_pos += std::string("var ytInitialPlayerResponse = ").length() -1; // Get to the '{'
        }

        // Fallback: Try to find "ytInitialData = {"
        if (start_pos == std::string::npos) {
            start_pos = html_content.find("ytInitialData = {");
             if (start_pos != std::string::npos) {
                start_pos += std::string("ytInitialData = ").length() -1;
            }
        }


        if (start_pos != std::string::npos) {
            int brace_count = 0;
            size_t end_pos = std::string::npos;
            for (size_t i = start_pos; i < html_content.length(); ++i) {
                if (html_content[i] == '{') {
                    brace_count++;
                } else if (html_content[i] == '}') {
                    brace_count--;
                    if (brace_count == 0) {
                        end_pos = i;
                        break;
                    }
                }
            }

            if (end_pos != std::string::npos) {
                json_str = html_content.substr(start_pos, end_pos - start_pos + 1);
                try {
                    json player_response = json::parse(json_str);

                    // --- Extracting data ---
                    // Title and Author (from videoDetails in ytInitialPlayerResponse)
                    if (player_response.contains("videoDetails")) {
                        const auto& videoDetails = player_response["videoDetails"];
                        if (videoDetails.contains("title") && videoDetails["title"].is_string()) {
                            info.title = videoDetails["title"].get<std::string>();
                        }
                        if (videoDetails.contains("author") && videoDetails["author"].is_string()) {
                            info.author = videoDetails["author"].get<std::string>();
                        }
                        if (videoDetails.contains("viewCount") && videoDetails["viewCount"].is_string()) {
                           try {
                                info.view_count = std::stol(videoDetails["viewCount"].get<std::string>());
                           } catch (const std::exception& e) {
                               std::cerr << "Warning: Could not parse view count: " << e.what() << std::endl;
                               info.view_count = 0;
                           }
                        }
                    }

                    // Formats (from streamingData in ytInitialPlayerResponse)
                    if (player_response.contains("streamingData")) {
                        const auto& streamingData = player_response["streamingData"];

                        auto process_format_list = [&](const json& format_list_json, bool is_adaptive) {
                            if (!format_list_json.is_array()) return;

                            for (const auto& fmt_json : format_list_json) {
                                VideoFormat fmt;

                                if (fmt_json.contains("itag") && fmt_json["itag"].is_number()) {
                                    fmt.itag = std::to_string(fmt_json["itag"].get<int>());
                                } else {
                                    // itag is essential, skip if missing
                                    std::cerr << "Warning: Skipping format entry without itag." << std::endl;
                                    continue;
                                }

                                // Quality
                                if (fmt_json.contains("qualityLabel") && fmt_json["qualityLabel"].is_string()) {
                                    fmt.quality = fmt_json["qualityLabel"].get<std::string>();
                                } else if (fmt_json.contains("quality") && fmt_json["quality"].is_string()) {
                                    fmt.quality = fmt_json["quality"].get<std::string>(); // e.g. medium, small for non-adaptive
                                } else if (fmt_json.contains("bitrate") && fmt_json["bitrate"].is_number()) { // Common for adaptive audio
                                    fmt.quality = std::to_string(fmt_json["bitrate"].get<int>() / 1000) + "kbps";
                                } else {
                                    fmt.quality = "N/A";
                                }

                                // MimeType, Container, Codecs, Type
                                if (fmt_json.contains("mimeType") && fmt_json["mimeType"].is_string()) {
                                    std::string mime = fmt_json["mimeType"].get<std::string>();
                                    size_t slash_pos = mime.find('/');
                                    size_t semicolon_pos = mime.find(';');

                                    if (slash_pos != std::string::npos) {
                                        std::string main_type = mime.substr(0, slash_pos);
                                        if (semicolon_pos != std::string::npos) {
                                            fmt.container = mime.substr(slash_pos + 1, semicolon_pos - (slash_pos + 1));
                                            std::string codecs_param = mime.substr(semicolon_pos + 1);
                                            size_t codecs_val_pos = codecs_param.find("codecs=\"");
                                            if (codecs_val_pos != std::string::npos) {
                                                size_t start_quote = codecs_param.find('"', codecs_val_pos);
                                                if (start_quote != std::string::npos) {
                                                    size_t end_quote = codecs_param.find('"', start_quote + 1);
                                                    if (end_quote != std::string::npos) {
                                                        fmt.codecs = codecs_param.substr(start_quote + 1, end_quote - (start_quote + 1));
                                                    } else {
                                                         fmt.codecs = codecs_param.substr(start_quote + 1); // Best effort
                                                    }
                                                }
                                            } else {
                                                fmt.codecs = codecs_param; // Might just be the value
                                            }
                                        } else {
                                            fmt.container = mime.substr(slash_pos + 1);
                                        }

                                        if (main_type == "audio") {
                                            fmt.type = is_adaptive ? "audio_only" : "audio"; // "audio" for muxed, "audio_only" for adaptive
                                        } else if (main_type == "video") {
                                             fmt.type = is_adaptive ? "video_only" : "video"; // "video" for muxed, "video_only" for adaptive
                                        } else {
                                            fmt.type = "unknown";
                                        }

                                    } else { // Malformed mimeType
                                        fmt.container = "unknown";
                                        fmt.type = "unknown";
                                    }
                                } else { // MimeType missing
                                    fmt.container = "N/A";
                                    fmt.codecs = "N/A";
                                    fmt.type = "N/A";
                                }

                                // URL or Cipher
                                if (fmt_json.contains("url") && fmt_json["url"].is_string()) {
                                    fmt.url = fmt_json["url"].get<std::string>();
                                } else if (fmt_json.contains("signatureCipher") && fmt_json["signatureCipher"].is_string()) {
                                    // TODO: Implement signature deciphering
                                    // For now, mark URL as needing deciphering and log
                                    fmt.url = "NEEDS_DECIPHERING";
                                    std::cout << "Note: Format itag " << fmt.itag << " requires signature deciphering (URL in signatureCipher)." << std::endl;
                                } else if (fmt_json.contains("cipher") && fmt_json["cipher"].is_string()) { // Older format for cipher
                                     fmt.url = "NEEDS_DECIPHERING_OLD";
                                     std::cout << "Note: Format itag " << fmt.itag << " requires signature deciphering (URL in cipher)." << std::endl;
                                }
                                else {
                                    fmt.url = ""; // No URL and no cipher means it's likely unusable
                                    std::cout << "Warning: Format itag " << fmt.itag << " has no URL or cipher." << std::endl;
                                }

                                info.formats.push_back(fmt);
                            }
                        };

                        if (streamingData.contains("formats") && streamingData["formats"].is_array()) {
                            process_format_list(streamingData["formats"], false); // Muxed streams
                        }
                        if (streamingData.contains("adaptiveFormats") && streamingData["adaptiveFormats"].is_array()) {
                             process_format_list(streamingData["adaptiveFormats"], true); // Adaptive streams
                        }
                    }
                    scraping_successful = true;
                    std::cout << "Successfully parsed video info using web scraping." << std::endl;

                } catch (const json::parse_error& e) {
                    std::cerr << "JSON parsing error: " << e.what() << std::endl;
                     // Print a snippet of the JSON string for debugging if it's not too long
                    std::cerr << "Problematic JSON snippet (up to 500 chars): " << json_str.substr(0, std::min(json_str.length(), (size_t)500)) << std::endl;

                }
            } else {
                std::cerr << "Could not find end of JSON blob." << std::endl;
            }
        } else {
            std::cerr << "Could not find ytInitialPlayerResponse or ytInitialData in HTML content." << std::endl;
        }
    } else {
        std::cerr << "Failed to fetch YouTube page. Status code: " << r.status_code << std::endl;
        if (!r.error.message.empty()) {
            std::cerr << "CPR Error: " << r.error.message << std::endl;
        }
    }

    if (scraping_successful) {
        return info;
    }

    // Fallback to API key method if scraping fails AND an API key is provided
    if (!api_key.empty()) {
        std::cout << "Web scraping failed, but API key provided. Attempting API call (placeholder)..." << std::endl;
        // **Actual API Implementation Would Be Here (as before)**
        if (video_id == "dQw4w9WgXcQ") {
            info.title = "Rick Astley - Never Gonna Give You Up (API Fallback)";
            info.author = "RickAstleyVEVO (API Fallback)";
            info.view_count = 1000000000;
            info.formats.clear();
            info.formats.push_back({"22", "720p (API)", "mp4", "avc1.64001F, mp4a.40.2", "video", "api_url_22"});
            info.formats.push_back({"18", "360p (API)", "mp4", "avc1.42001E, mp4a.40.2", "video", "api_url_18"});
        } else {
            info.title = "Unknown Video (API Fallback)";
            info.author = "Unknown Author (API Fallback)";
            info.view_count = 0;
            info.formats.clear();
        }
        return info;
    }

    std::cerr << "Failed to fetch video info. No successful method (scraping or API key)." << std::endl;
    return info; // Return empty or partially filled info
}

void display_video_info(const VideoInfo& info) {
    if (info.title.empty() && info.id.empty()) {
        std::cout << "No video information to display." << std::endl;
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
            std::cout << "Itag: " << fmt.itag << ", Type: " << fmt.type
                      << ", Quality: " << fmt.quality << ", Container: " << fmt.container
                      << ", Codecs: " << fmt.codecs << std::endl;
        }
    } else {
        std::cout << "No format information available (or not implemented yet)." << std::endl;
    }
    std::cout << "-------------------------" << std::endl;
}

// Placeholder function for downloading a video format
bool download_video_format(const VideoInfo& video_info, const VideoFormat& format_to_download, const std::string& output_dir = ".") {
    if (format_to_download.url.empty() || format_to_download.url == "example_url") { // Check against placeholder
        std::cerr << "Error: Download URL for itag " << format_to_download.itag << " is missing or invalid (placeholder)." << std::endl;
        return false;
    }

    // Construct a filename. e.g., "VideoTitle_itag.container"
    std::string base_filename = sanitize_filename(video_info.title.empty() ? video_info.id : video_info.title);
    std::string filename = output_dir + "/" + base_filename + "_" + format_to_download.itag + "." + format_to_download.container;

    std::cout << "Attempting to download format " << format_to_download.itag
              << " for video '" << video_info.title << "'"
              << " from URL: " << format_to_download.url
              << " to " << filename
              << " (Download not implemented yet)" << std::endl;

    // **Actual Download Implementation Would Be Here**
    // 1. Create an output file stream: std::ofstream outfile(filename, std::ios::binary);
    // 2. Make HTTP GET request using a library like cpr, with streaming support.
    //    Example conceptual cpr usage for streaming download:
    //    cpr::Response r = cpr::Download(outfile, cpr::Url{format_to_download.url});
    //    if (r.status_code == 200 && r.error.code == cpr::ErrorCode::OK) {
    //        std::cout << "Download completed successfully: " << filename << std::endl;
    //        return true;
    //    } else {
    //        std::cerr << "Download failed. Status: " << r.status_code << ", Error: " << r.error.message << std::endl;
    //        // Potentially delete partially downloaded file: std::remove(filename.c_str());
    //        return false;
    //    }

    // --- Placeholder behavior ---
    std::cout << "Simulating download for " << filename << "..." << std::endl;
    // Create a dummy file to signify download
    std::ofstream dummy_outfile(filename);
    if (dummy_outfile) {
        dummy_outfile << "This is a placeholder for the downloaded video content for itag "
                      << format_to_download.itag << " of video " << video_info.title << std::endl;
        dummy_outfile.close();
        std::cout << "Placeholder file created: " << filename << std::endl;
        return true;
    } else {
        std::cerr << "Failed to create placeholder file: " << filename << std::endl;
        return false;
    }
    // --- End Placeholder behavior ---
}


void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <video_url_or_id> [options]\n"
              << "Options:\n"
              << "  -h, --help          Show this help message\n"
              << "  -f, --format <itag> Specify video format itag for download\n"
              << "  --api-key <key>     Your YouTube API Key (optional, for fallback or specific features)\n"
              << "  -o, --output <dir>  Output directory for downloads (defaults to current dir)\n";
}

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::string video_url_or_id;
    std::string selected_format_itag;
    std::string api_key;
    std::string output_directory = ".";

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-h" || args[i] == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (args[i] == "-f" || args[i] == "--format") {
            if (i + 1 < args.size()) {
                selected_format_itag = args[++i];
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (itag)." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        } else if (args[i] == "--api-key") {
            if (i + 1 < args.size()) {
                api_key = args[++i];
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (API key)." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        } else if (args[i] == "-o" || args[i] == "--output") {
            if (i + 1 < args.size()) {
                output_directory = args[++i];
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (directory)." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        }
         else if (video_url_or_id.empty()) {
            video_url_or_id = args[i];
        } else {
            std::cerr << "Error: Unknown argument or too many URLs/IDs: " << args[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (video_url_or_id.empty()) {
        std::cerr << "Error: Video URL or ID is required." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // API key is now optional. If not provided by arg, check env.
    // If still not found, it remains empty, and fetch_video_info will primarily try scraping.
    if (api_key.empty()) {
        const char* env_api_key = std::getenv("YOUTUBE_API_KEY");
        if (env_api_key != nullptr) {
            api_key = env_api_key;
            std::cout << "Using optional API key from YOUTUBE_API_KEY environment variable." << std::endl;
        }
    }
    if (!api_key.empty()) {
        std::cout << "Optional API key provided. It may be used if web scraping fails or for specific features." << std::endl;
    } else {
        std::cout << "No API key provided. Relying primarily on web scraping." << std::endl;
    }

    std::cout << "Project Name: " << PROJECT_NAME << std::endl;

    std::string video_id = extract_video_id(video_url_or_id);
    if (video_id.empty() && !video_url_or_id.empty()) {
        std::cout << "Could not extract video ID from input, assuming input is already a video ID: " << video_url_or_id << std::endl;
        video_id = video_url_or_id;
    }

    if (video_id.empty()) {
        std::cerr << "Error: Could not determine video ID." << std::endl;
        return 1;
    }

    std::cout << "Processing video ID: " << video_id << std::endl;

    VideoInfo video_info = fetch_video_info(video_id, api_key);
    if (video_info.title.empty() && video_info.id != video_id) { // Check if fetch_video_info returned empty/error
        std::cerr << "Failed to fetch video info. Exiting." << std::endl;
        return 1;
    }
    display_video_info(video_info);

    if (!selected_format_itag.empty()) {
        const VideoFormat* format_to_download = nullptr;
        for (const auto& fmt : video_info.formats) {
            if (fmt.itag == selected_format_itag) {
                format_to_download = &fmt;
                break;
            }
        }

        if (format_to_download != nullptr) {
            std::cout << "\nAttempting download for selected itag: " << selected_format_itag << std::endl;
            if (download_video_format(video_info, *format_to_download, output_directory)) {
                std::cout << "Download process for itag " << selected_format_itag << " initiated successfully (simulated)." << std::endl;
            } else {
                std::cerr << "Download process for itag " << selected_format_itag << " failed." << std::endl;
            }
        } else {
            std::cerr << "Error: Selected format itag " << selected_format_itag << " not found in available formats." << std::endl;
        }
    } else {
        std::cout << "\nNo format selected for download (use -f <itag> to download)." << std::endl;
    }

    return 0;
}
