#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <iomanip>
#include <algorithm>
#include <cstdio>
#include <filesystem> // For std::filesystem::exists

#include "cxxopts.hpp"
#include "core/youtube_fetcher.h"
#include "core/video_info.h"

// (formatBytes, displayProgressBar - assumed to be present and correct)
// Helper function to format file size (from previous step)
std::string formatBytes(long long bytes) {
    if (bytes < 0) return "N/A";
    const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double dblBytes = bytes;
    if (bytes == 0) return "0 B"; // Handle 0 bytes case
    while (dblBytes >= 1024 && i < 4) {
        dblBytes /= 1024;
        i++;
    }
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.2f %s", dblBytes, suffixes[i]);
    return std::string(buffer);
}

// Simple progress bar display
void displayProgressBar(long long current, long long total) {
    const int barWidth = 70;
    float progress = 0.0f;
    if (total > 0) {
        progress = static_cast<float>(current) / total;
    } else if (current > 0) { // If total is unknown, just show current downloaded
        std::cout << "\rDownloaded: " << formatBytes(current) << "     ";
        std::flush(std::cout);
        return;
    } else { // No info yet
        std::cout << "\rWaiting for download to start... ";
        std::flush(std::cout);
        return;
    }

    int pos = static_cast<int>(barWidth * progress);
    std::cout << "\r[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << (progress * 100.0) << "% "
              << "(" << formatBytes(current) << "/" << formatBytes(total) << ")";
    std::flush(std::cout);
}


// Function to sanitize a string to be used as a filename
std::string sanitizeFilename(const std::string& input, size_t maxLength = 200) {
    std::string output = input;

    // Replace invalid characters
    std::replace_if(output.begin(), output.end(), [](char c){
        return std::string("<>:\"/\\|?*").find(c) != std::string::npos || (unsigned char)c < 32;
    }, '_');

    // Remove leading/trailing whitespace and dots
    output.erase(0, output.find_first_not_of(" \t\n\r\f\v."));
    output.erase(output.find_last_not_of(" \t\n\r\f\v.") + 1);

    // Truncate if too long (common OS limit is 255-260 for full path)
    if (output.length() > maxLength) {
        output = output.substr(0, maxLength);
        // Ensure it doesn't end with a space or dot after truncation
        output.erase(output.find_last_not_of(" \t\n\r\f\v.") + 1);
    }

    if (output.empty()) {
        return "downloaded_file";
    }
    return output;
}

// Function to get a file extension from mime type (assumed present)
std::string getExtensionFromMimeType(const std::string& mimeType) {
    if (mimeType.find("video/mp4") != std::string::npos) return ".mp4";
    if (mimeType.find("video/x-matroska") != std::string::npos) return ".mkv";
    if (mimeType.find("video/webm") != std::string::npos) return ".webm";
    if (mimeType.find("audio/mp4") != std::string::npos) return ".m4a";
    if (mimeType.find("audio/webm") != std::string::npos) return ".webm";
    if (mimeType.find("audio/mpeg") != std::string::npos) return ".mp3";
    if (mimeType.find("audio/ogg") != std::string::npos) return ".ogg";
    if (mimeType.find("audio/wav") != std::string::npos) return ".wav";
    return ".bin";
}


// displayFormats function (assumed present and correct)
void displayFormats(const yt_core::VideoDetails& details) {
    std::cout << "\n--- Available Formats ---" << std::endl;
    int counter = 1;

    auto print_stream_details = [&](const yt_core::MediaStream& stream, int current_idx) {
        std::cout << "  " << std::setw(2) << current_idx << ". ";
        std::cout << "itag: " << std::setw(3) << stream.itag;

        std::string quality_str;
        if (stream.qualityLabel.has_value() && !stream.qualityLabel.value().empty()) {
            quality_str = stream.qualityLabel.value();
        } else if (stream.width.has_value() && stream.height.has_value()) {
            quality_str = std::to_string(stream.width.value()) + "x" + std::to_string(stream.height.value());
            if (stream.fps.has_value()) {
                quality_str += "p" + std::to_string(stream.fps.value());
            } else {
                quality_str += "p";
            }
        }
        if (!quality_str.empty()) std::cout << " | " << std::setw(10) << std::left << quality_str;
        else std::cout << " | " << std::setw(10) << std::left << "N/A";

        std::string type_str;
        if (stream.isAudioOnly) {
            type_str = "Audio";
            if (stream.audioQuality.has_value()) {
                 type_str += " (" + stream.audioQuality.value() + ")";
            }
        } else if (stream.isVideoOnly) {
            type_str = "Video Only";
        } else {
             type_str = "Muxed A/V";
        }
        std::cout << " | " << std::setw(18) << std::left << type_str;

        if (stream.codecs.has_value() && !stream.codecs.value().empty()) {
            std::string codecs_short = stream.codecs.value();
            size_t first_dot = codecs_short.find_first_of('.');
            size_t first_comma = codecs_short.find_first_of(',');
            if (first_comma != std::string::npos && (first_dot == std::string::npos || first_comma < first_dot)) {
                codecs_short = codecs_short.substr(0, first_comma);
            } else if (first_dot != std::string::npos) {
                codecs_short = codecs_short.substr(0, first_dot);
            }
            std::cout << " (" << std::setw(8) << std::left << codecs_short << ")";
        } else {
            std::cout << " " << std::setw(10) << std::left << " "; // Keep alignment
        }

        if (stream.bitrate > 0) {
            std::cout << " | ~" << std::setw(4) << std::right << (stream.bitrate / 1000) << "kbps";
        } else {
            std::cout << " | " << std::setw(10) << " "; // Keep alignment
        }
        if (stream.contentLength.has_value()) {
            std::cout << " | " << std::setw(10) << std::right << formatBytes(stream.contentLength.value());
        } else {
            std::cout << " | " << std::setw(10) << std::right << "N/A";
        }
        std::cout << std::endl;
    };
    std::cout << std::left; // Align subsequent text to the left for this block

    std::cout << "\n[Muxed Streams (Audio+Video)]" << std::endl;
    if (!details.formats.empty()) {
        for (const auto& stream : details.formats) {
            print_stream_details(stream, counter++);
        }
    } else {
        std::cout << "  No muxed streams available." << std::endl;
    }

    std::cout << "\n[Adaptive Streams (DASH)]" << std::endl;
    if (!details.adaptiveFormats.empty()) {
        for (const auto& stream : details.adaptiveFormats) {
            print_stream_details(stream, counter++);
        }
    } else {
        std::cout << "  No adaptive streams available." << std::endl;
    }
    std::cout << std::right; // Reset alignment
}


bool isValidYouTubeUrl(const std::string& url) {
    // Basic check, not exhaustive
    return url.find("youtube.com/") != std::string::npos || url.find("youtu.be/") != std::string::npos;
}


int main(int argc, char* argv[]) {
    cxxopts::Options options("yt-cli", "YouTube Format Converter CLI - C++ Clone of yt-dlp (simplified)\nVersion 0.1.0");
    options.set_width(100);
    options.add_options()
        ("h,help", "Print usage")
        ("u,url", "YouTube video URL (required)", cxxopts::value<std::string>())
        ("i,info", "Only display video info, do not ask to download", cxxopts::value<bool>()->default_value("false"))
        ("o,output", "Output filename. Default: <video_title>_<quality>_<itag>.<ext>", cxxopts::value<std::string>())
        ("y,yes", "Automatically overwrite output file if it exists", cxxopts::value<bool>()->default_value("false"))
    ;
    options.positional_help("<video_url>");
    options.parse_positional({"url"});

    auto result = options.parse(argc, argv);

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("url")) {
        std::cerr << "Error: YouTube video URL is required. Use -u <url> or provide as positional argument." << std::endl;
        std::cerr << "Use --help for more information." << std::endl;
        return 1;
    }

    std::string videoUrl = result["url"].as<std::string>();
    if (!isValidYouTubeUrl(videoUrl)) {
        std::cerr << "Warning: The provided URL does not look like a standard YouTube URL: " << videoUrl << std::endl;
        // Allow proceeding, but warn user.
    }

    bool info_only = result["info"].as<bool>();
    bool auto_overwrite = result["yes"].as<bool>();

    std::cout << "Fetching video information for: " << videoUrl << "..." << std::endl;

    yt_core::YouTubeFetcher fetcher;
    auto videoDetailsOpt = fetcher.fetchVideoDetails(videoUrl);

    if (!videoDetailsOpt) {
        std::cerr << "Error: Failed to fetch video details. Possible reasons:" << std::endl;
        std::cerr << "  - Network issue (check internet connection)." << std::endl;
        std::cerr << "  - Invalid or private YouTube URL." << std::endl;
        std::cerr << "  - YouTube API changes (program might need an update)." << std::endl;
        return 1;
    }

    const auto& details = videoDetailsOpt.value();

    std::cout << "\n--- Video Details ---" << std::endl;
    std::cout << "Title: " << details.title << std::endl;
    std::cout << "Author: " << details.author << std::endl;
    std::cout << "Duration: " << details.lengthSeconds << " seconds" << std::endl;

    displayFormats(details);

    if (info_only) {
        return 0;
    }

    std::vector<yt_core::MediaStream> all_streams;
    all_streams.insert(all_streams.end(), details.formats.begin(), details.formats.end());
    all_streams.insert(all_streams.end(), details.adaptiveFormats.begin(), details.adaptiveFormats.end());

    if (all_streams.empty()) {
        std::cout << "\nNo downloadable streams found for this video." << std::endl;
        return 0;
    }

    int choice_num = 0;
    while (true) {
        std::cout << "\nEnter the number of the format to download (or 0 to exit): ";
        std::cin >> choice_num;
        if (!std::cin) {
            std::cout << "Invalid input. Please enter a number." << std::endl;
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        if (choice_num >= 0 && choice_num <= static_cast<int>(all_streams.size())) {
            break;
        }
        std::cout << "Invalid choice. Please select a number from the list (1 to "
                  << all_streams.size() << ") or 0 to exit." << std::endl;
    }

    if (choice_num == 0) {
        std::cout << "Exiting." << std::endl;
        return 0;
    }

    const yt_core::MediaStream& selectedStream = all_streams[choice_num - 1];

    std::string outputFilename;
    if (result.count("output")) {
        outputFilename = result["output"].as<std::string>();
    } else {
        std::string title_part = details.title.empty() ? "video" : details.title;
        title_part = sanitizeFilename(title_part, 80); // Keep title part reasonably short

        std::string quality_label;
        if(selectedStream.qualityLabel.has_value() && !selectedStream.qualityLabel.value().empty()) {
            quality_label = selectedStream.qualityLabel.value();
        } else if (selectedStream.width.has_value() && selectedStream.height.has_value()) {
            quality_label = std::to_string(selectedStream.height.value_or(0)) + "p";
             if (selectedStream.fps.has_value()) quality_label += std::to_string(selectedStream.fps.value());
        } else if (selectedStream.isAudioOnly && selectedStream.audioQuality.has_value()) {
            quality_label = selectedStream.audioQuality.value_or("audio");
        } else {
            quality_label = "fmt" + std::to_string(selectedStream.itag);
        }
        quality_label = sanitizeFilename(quality_label, 30);

        std::string ext = getExtensionFromMimeType(selectedStream.mimeType);
        outputFilename = title_part + "_" + quality_label + "_" + std::to_string(selectedStream.itag) + ext;
        outputFilename = sanitizeFilename(outputFilename); // Final sanitization for the whole name
    }

    if (std::filesystem::exists(outputFilename) && !auto_overwrite) {
        // Clear potential error flags from previous std::cin operations
        std::cin.clear();
        // Ignore any leftover characters in the input buffer, especially the newline from previous number input
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        std::cout << "File '" << outputFilename << "' already exists. Overwrite? (y/N): ";
        char yn_choice_char; // Read as char
        std::cin.get(yn_choice_char); // Use get to read a single char

        if (std::tolower(yn_choice_char) != 'y') {
            std::cout << "Download cancelled by user." << std::endl;
            return 0;
        }
         // Consume rest of the line if user types more than one char e.g. "yes"
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }


    std::cout << "\nSelected format #" << choice_num << ":" << std::endl;
    std::cout << "  itag: " << selectedStream.itag << std::endl;
    std::cout << "  URL: " << selectedStream.url.substr(0, 70) << (selectedStream.url.length() > 70 ? "..." : "") << std::endl;
    std::cout << "  Type: " << selectedStream.mimeType << std::endl;
    if (selectedStream.contentLength.has_value()) {
        std::cout << "  Size: " << formatBytes(selectedStream.contentLength.value()) << std::endl;
    }
    std::cout << "  Output to: " << outputFilename << std::endl;

    std::cout << "\nStarting download..." << std::endl;
    displayProgressBar(0, selectedStream.contentLength.value_or(0)); // Initial bar

    bool success = fetcher.downloadStream(selectedStream, outputFilename,
        [&](long long current, long long total) {
            displayProgressBar(current, total);
        }
    );

    std::cout << std::endl;

    if (success) {
        std::cout << "Download completed successfully: " << outputFilename << std::endl;
    } else {
        std::cout << "Download failed for: " << outputFilename << std::endl;
        std::cout << "Possible reasons:" << std::endl;
        std::cout << "  - Network interruption or server error." << std::endl;
        std::cout << "  - Insufficient disk space or write permissions." << std::endl;
        std::cout << "  - URL expired (especially for very long videos if there was a delay)." << std::endl;
    }

    return success ? 0 : 1;
}
