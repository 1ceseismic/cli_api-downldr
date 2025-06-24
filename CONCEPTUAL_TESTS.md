# Conceptual Test Cases for yt-cli-downloader

This document outlines conceptual test cases for the `yt-cli-downloader` tool.
The primary method for fetching video information is now **web page scraping (keyless)**.
An API key is optional and serves as a fallback if scraping fails AND the key is provided.

## 1. Video ID Extraction (`extract_video_id` function)

(This section remains unchanged as `extract_video_id` logic has not been altered.)

*   **Test Case 1.1:** Standard `youtube.com/watch?v=` URL
    *   Input: `"https://www.youtube.com/watch?v=dQw4w9WgXcQ"`
    *   Expected Output: `"dQw4w9WgXcQ"`
*   **Test Case 1.2:** `youtu.be/` short URL
    *   Input: `"https://youtu.be/dQw4w9WgXcQ"`
    *   Expected Output: `"dQw4w9WgXcQ"`
*   **Test Case 1.3:** URL with extra query parameters (`&list=`, `&t=`)
    *   Input: `"https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PL...&t=60s"`
    *   Expected Output: `"dQw4w9WgXcQ"`
*   **Test Case 1.4:** `youtu.be/` URL with extra query parameters
    *   Input: `"https://youtu.be/dQw4w9WgXcQ?t=60s"`
    *   Expected Output: `"dQw4w9WgXcQ"`
*   **Test Case 1.5:** Invalid or non-YouTube URL
    *   Input: `"https://www.example.com"`
    *   Expected Output: `""` (empty string, and a warning might be logged)
*   **Test Case 1.6:** URL with video ID containing hyphens and underscores
    *   Input: `"https://www.youtube.com/watch?v=abc-123_XYZ"`
    *   Expected Output: `"abc-123_XYZ"`
*   **Test Case 1.7:** Input is already a valid video ID
    *   Input: `"dQw4w9WgXcQ"`
    *   Expected Output: `""` (main logic uses input as ID if extraction fails)

## 2. Command-Line Argument Parsing (in `main`)

*   **Test Case 2.1:** No arguments
    *   Command: `yt-cli-downloader`
    *   Expected: Prints usage, exits non-zero.
*   **Test Case 2.2:** Help option
    *   Command: `yt-cli-downloader --help` (or `-h`)
    *   Expected: Prints usage, exits zero.
*   **Test Case 2.3 (NEW):** Video URL/ID only (keyless, primary path)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"` (ensure `YOUTUBE_API_KEY` is unset)
    *   Expected: Proceeds to fetch info using web scraping. Output should indicate "No API key provided. Relying primarily on web scraping."
*   **Test Case 2.4:** Video URL/ID and optional API key
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY"`
    *   Expected: Proceeds to fetch info, primarily trying web scraping. Output should indicate "Optional API key provided."
*   **Test Case 2.5:** Video URL/ID, (optional API key), and format selection
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" -f "22"`
    *   Expected: Fetches info (scraping), then attempts to "download" format "22".
*   **Test Case 2.6:** All valid options (keyless)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" -f "22" -o "/tmp/downloads"`
    *   Expected: Fetches info (scraping), attempts "download" to `/tmp/downloads`.
*   **Test Case 2.7 - 2.10 (Unchanged):** Missing arguments for options, unknown option.
    *   (These remain relevant for general CLI robustness)
*   **Test Case 2.11 (NEW):** Optional API key from environment `YOUTUBE_API_KEY`
    *   Setup: `export YOUTUBE_API_KEY="ENV_API_KEY"`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"`
    *   Expected: Proceeds to fetch info (scraping first). Output indicates API key found from env.
    *   Cleanup: `unset YOUTUBE_API_KEY`
*   **Test Case 2.12 (Unchanged):** API key from argument overrides environment.
    *   (Relevant if API key is used as fallback)

## 3. Information Fetching (`fetch_video_info` function)

This is the core logic change. Assumes HTTP calls for scraping would work in a real environment.

*   **Test Case 3.1 (NEW):** Successful web scraping (no API key provided)
    *   Scenario: Call `fetch_video_info("dQw4w9WgXcQ", "")`. Mock a successful HTTP GET and valid `ytInitialPlayerResponse`.
    *   Expected: `VideoInfo` populated with data extracted from the (mocked) webpage. Logs success message for scraping.
*   **Test Case 3.2 (NEW):** Web scraping fails, no API key provided
    *   Scenario: Call `fetch_video_info("some_id", "")`. Mock a failed HTTP GET (e.g., 404) or unparsable/missing JSON blob.
    *   Expected: Returns an empty or partially filled `VideoInfo`. Logs error about scraping failure and "No successful method".
*   **Test Case 3.3 (NEW):** Web scraping fails, API key provided (triggers API fallback)
    *   Scenario: Call `fetch_video_info("dQw4w9WgXcQ", "MY_API_KEY")`. Mock failed scraping.
    *   Expected: Logs scraping failure. Then, attempts API call (current placeholder API logic is used). `VideoInfo` populated by API placeholder data.
*   **Test Case 3.4 (NEW):** Successful web scraping, API key also provided
    *   Scenario: Call `fetch_video_info("dQw4w9WgXcQ", "MY_API_KEY")`. Mock successful scraping.
    *   Expected: `VideoInfo` populated from scraping. API key is not used for the primary fetch. Logs successful scraping.
*   **Test Case 3.5 (NEW):** Invalid Video ID for scraping
    *   Scenario: Call `fetch_video_info("invalidVideoIdHere", "")`. Mock HTTP GET returning a YouTube page indicating video not found.
    *   Expected: Scraping fails to find player response or extracts error data. Returns empty/error `VideoInfo`.
*   **Test Case 3.6 (NEW):** Format requires signature deciphering
    *   Scenario: Mock `ytInitialPlayerResponse` where a desired format has a `signatureCipher` field instead of `url`.
    *   Expected: The `VideoFormat.url` for that format is set to "NEEDS_DECIPHERING" (or similar). A note is logged.

## 4. Filename Sanitization (`sanitize_filename` function)

(This section remains unchanged.)

*   **Test Case 4.1 - 4.5 (Unchanged)**

## 5. Download Logic (Conceptual due to placeholders)

(This section remains largely unchanged but emphasizes that info comes from scraping by default.)

*   **Test Case 5.1:** Download with a valid, known itag (keyless)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" -f "22"`
    *   Expected: Info fetched via scraping. `download_video_format` called. Placeholder file created.
*   **Test Case 5.2 - 5.5 (Largely Unchanged):**
    *   These tests for invalid itag, invalid URL in format, output directory, and info fetch failure are still relevant. The source of `VideoInfo` is now primarily scraping.
    *   For 5.5 (fetch failure), this now means scraping failed AND no API key was provided or API fallback also failed.

## 6. Specific Format Extraction Edge Cases (Conceptual)

*   **Test Case 6.1:** MimeType parsing variations
    *   Scenario: Test with mimeTypes like "video/mp4", "audio/webm; codecs=\"opus\"", "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"".
    *   Expected: `container`, `codecs`, and `type` fields in `VideoFormat` are populated correctly.
*   **Test Case 6.2:** Missing optional fields in format JSON
    *   Scenario: A format in `ytInitialPlayerResponse` is missing `qualityLabel` or `codecs` part of `mimeType`.
    *   Expected: `VideoFormat` fields get default values like "N/A" or empty, without crashing.
*   **Test Case 6.3:** Video with no muxed streams (only adaptive)
    *   Scenario: `streamingData.formats` is empty or missing, but `streamingData.adaptiveFormats` has entries.
    *   Expected: Tool correctly extracts adaptive formats.

This list is not exhaustive but covers the main logical paths for the keyless approach.
Actual implementation of these tests in a C++ framework would involve
mocking HTTP responses and file system interactions.
