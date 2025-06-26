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
    std::string quality;
    std::string container;
    std::string codecs;
    std::string type;
    std::string url;
    long long filesize = 0; // Initialize to 0, will be populated from yt-dlp
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

                bool has_video = (vcodec != "none" && !vcodec.empty());
                bool has_audio = (acodec != "none" && !acodec.empty());

                if (has_video && has_audio) { fmt.type = "video/audio"; }
                else if (has_video) { fmt.type = "video_only"; }
                else if (has_audio) { fmt.type = "audio_only"; }
                else { fmt.type = "unknown"; }

                // Populate filesize
                if (fmt_json.contains("filesize") && fmt_json["filesize"].is_number()) {
                    fmt.filesize = fmt_json["filesize"].get<long long>();
                } else if (fmt_json.contains("filesize_approx") && fmt_json["filesize_approx"].is_number()) {
                    fmt.filesize = fmt_json["filesize_approx"].get<long long>();
                } else {
                    fmt.filesize = 0; // Or -1 to indicate unknown, but 0 is fine for now
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
            std::cout << "Itag: " << fmt.itag
                      << ", Type: " << fmt.type
                      << ", Quality: " << fmt.quality
                      << ", Container: " << fmt.container
                      << ", Codecs: " << fmt.codecs
                      << ", Size: " << (fmt.filesize > 0 ? format_bytes(fmt.filesize) : "N/A")
                      << std::endl;
        }
    } else {
        std::cout << "No format information available (or yt-dlp found no suitable formats)." << std::endl;
    }
    std::cout << "-------------------------" << std::endl;
}

// Function for downloading a video format
bool download_video_format(const VideoInfo& video_info, const VideoFormat& format_to_download, const std::string& output_dir = ".") {
    if (format_to_download.url.empty()) {
        std::cerr << "Error: Download URL for itag " << format_to_download.itag << " is empty." << std::endl;
        return false;
    }

    std::string base_filename = sanitize_filename(video_info.title.empty() ? video_info.id : video_info.title);
    std::string filename_extension = format_to_download.container;
    size_t semicolon_pos = filename_extension.find(';');
    if (semicolon_pos != std::string::npos) {
        filename_extension = filename_extension.substr(0, semicolon_pos);
    }
    std::string filename = output_dir + "/" + base_filename + "_" + format_to_download.itag + "." + filename_extension;

    std::cout << "Attempting to download format " << format_to_download.itag
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


void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <video_url_or_id> [options]\n"
              << "Options:\n"
              << "  -h, --help          Show this help message\n"
              << "  -f, --format <itag> Specify video format itag for download\n"
              << "  -o, --output <dir>  Output directory for downloads (defaults to current dir)\n"
              << "Requires yt-dlp to be installed and in PATH.\n";
}

int main(int argc, char **argv) {
    std::cout << PROJECT_NAME << " - YouTube CLI Downloader" << std::endl;
    std::cout << "-------------------------------------------" << std::endl;
    std::cout << "This tool relies on 'yt-dlp' being installed and accessible in your system's PATH." << std::endl;

    if (!check_ytdlp_availability()) {
        return 1; // Exit if yt-dlp is not available
    }
    std::cout << "-------------------------------------------" << std::endl;


    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || (args.size() == 1 && (args[0] == "-h" || args[0] == "--help"))) {
        print_usage(argv[0]);
        return args.empty() ? 1 : 0;
    }

    std::string video_url_or_id_arg;
    std::string selected_format_itag;
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
        } else if (args[i] == "-o" || args[i] == "--output") {
            if (i + 1 < args.size()) {
                output_directory = args[++i];
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument (directory)." << std::endl;
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
                std::cout << "Download process for itag " << selected_format_itag << " completed." << std::endl;
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
