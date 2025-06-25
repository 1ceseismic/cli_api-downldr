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

enum class StreamTypePreference {
    ANY,
    VIDEO_ONLY,
    AUDIO_ONLY,
    MUXED
};

enum class QualityPreference {
    NONE,
    BEST_RESOLUTION,
    WORST_RESOLUTION,
    BEST_BITRATE,
    WORST_BITRATE,
    BEST_AUDIO_BITRATE,
    WORST_AUDIO_BITRATE
};

struct FormatSelectionCriteria {
    StreamTypePreference stream_type = StreamTypePreference::ANY;
    QualityPreference quality_preference = QualityPreference::NONE;
    std::optional<int> target_height; // For specific resolution, e.g., 1080
    std::optional<int> target_fps;
    std::optional<std::string> preferred_codec_video; // e.g., "av01", "vp9", "avc1"
    std::optional<std::string> preferred_codec_audio; // e.g., "opus", "aac"
    bool prefer_adaptive_over_muxed = true; // Default behavior often prefers adaptive for best quality.
};

} // namespace yt_core

#endif // VIDEO_INFO_H
