#include <iostream>
#include <string>
#include <vector>

#define PROJECT_NAME "yt-cli-downloader"

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <video_url> [options]\n"
              << "Options:\n"
              << "  -h, --help    Show this help message\n"
              << "  -f, --format  Specify video format (not implemented yet)\n";
}

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::string video_url;

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-h" || args[i] == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (args[i] == "-f" || args[i] == "--format") {
            if (i + 1 < args.size()) {
                // Placeholder for format selection
                std::cout << "Format selection (not implemented yet): " << args[i+1] << std::endl;
                i++; // Consume the argument value
            } else {
                std::cerr << "Error: " << args[i] << " option requires an argument." << std::endl;
                print_usage(argv[0]);
                return 1;
            }
        } else if (video_url.empty()) {
            video_url = args[i];
        } else {
            std::cerr << "Error: Unknown argument or too many URLs: " << args[i] << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (video_url.empty()) {
        std::cerr << "Error: Video URL is required." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "Project Name: " << PROJECT_NAME << std::endl;
    std::cout << "Processing video URL: " << video_url << std::endl;

    // Future steps:
    // 1. Fetch video info using video_url
    // 2. Display available formats
    // 3. Allow user to select format (if not provided via -f)
    // 4. Download video

    return 0;
}
