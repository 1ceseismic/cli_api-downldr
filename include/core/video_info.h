#ifndef VIDEO_INFO_H
#define VIDEO_INFO_H

#include <string>
#include <vector>
#include <optional>

namespace yt_core {

struct MediaStream {
    int itag = 0;
    std::string url;
    std::string mimeType;
    std::string codecs;
    long bitrate = 0;

    std::optional<int> width;
    std::optional<int> height;
    std::optional<std::string> qualityLabel;
    std::optional<int> fps;

    std::optional<std::string> audioQuality;
    std::optional<long> audioSampleRate;
    std::optional<int> audioChannels;

    std::optional<long long> contentLength;
    bool isDash = true;
    bool isAudioOnly = false;
    bool isVideoOnly = false;
};

struct VideoDetails {
    std::string id;
    std::string title;
    std::string author;
    std::string channelId;
    long lengthSeconds = 0;
    std::string description;
    std::vector<std::string> thumbnails;

    std::vector<MediaStream> formats; // Muxed streams
    std::vector<MediaStream> adaptiveFormats; // DASH streams
};

} // namespace yt_core

#endif // VIDEO_INFO_H
