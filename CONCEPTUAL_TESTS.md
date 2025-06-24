# Conceptual Test Cases for yt-cli-downloader

This document outlines conceptual test cases for the `yt-cli-downloader` tool.
Since the development environment does not allow for compiling and running C++ tests,
these serve as a guide for manual testing or for future implementation in a C++
testing framework (e.g., Google Test, Catch2).

## 1. Video ID Extraction (`extract_video_id` function)

The `extract_video_id` function is responsible for getting the unique video ID
from various YouTube URL formats.

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
    *   Expected Output: `""` (as it doesn't match URL patterns, though main logic handles this by using the input as ID if extraction fails)

## 2. Command-Line Argument Parsing (in `main`)

These tests cover how the application handles different command-line arguments.
Assume `yt-cli-downloader` is the executable name.

*   **Test Case 2.1:** No arguments
    *   Command: `yt-cli-downloader`
    *   Expected: Prints usage instructions to `stderr`, exits with non-zero status.
*   **Test Case 2.2:** Help option
    *   Command: `yt-cli-downloader --help` (or `-h`)
    *   Expected: Prints usage instructions to `stdout` or `stderr`, exits with zero status.
*   **Test Case 2.3:** Video URL/ID only (missing API key)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"`
    *   Expected: Error message about missing API key, prints usage, exits non-zero. (Unless YOUTUBE_API_KEY is set).
*   **Test Case 2.4:** Video URL/ID and API key
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY"`
    *   Expected: Proceeds to fetch info (placeholder behavior).
*   **Test Case 2.5:** Video URL/ID, API key, and format selection
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY" -f "22"`
    *   Expected: Fetches info, then attempts to "download" format "22" (placeholder behavior).
*   **Test Case 2.6:** All valid options
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY" -f "22" -o "/tmp/downloads"`
    *   Expected: Fetches info, attempts to "download" format "22" into `/tmp/downloads`.
*   **Test Case 2.7:** Missing argument for `-f`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY" -f`
    *   Expected: Error message about missing argument for `-f`, prints usage, exits non-zero.
*   **Test Case 2.8:** Missing argument for `--api-key`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key`
    *   Expected: Error message about missing argument for `--api-key`, prints usage, exits non-zero.
*   **Test Case 2.9:** Missing argument for `-o`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY" -o`
    *   Expected: Error message about missing argument for `-o`, prints usage, exits non-zero.
*   **Test Case 2.10:** Unknown option
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "YOUR_API_KEY" --unknown-option`
    *   Expected: Error message about unknown option, prints usage, exits non-zero.
*   **Test Case 2.11:** API key from environment variable `YOUTUBE_API_KEY`
    *   Setup: `export YOUTUBE_API_KEY="ENV_API_KEY"`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"`
    *   Expected: Proceeds to fetch info using "ENV_API_KEY".
    *   Cleanup: `unset YOUTUBE_API_KEY`
*   **Test Case 2.12:** API key from argument overrides environment variable
    *   Setup: `export YOUTUBE_API_KEY="ENV_API_KEY"`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "ARG_API_KEY"`
    *   Expected: Proceeds to fetch info using "ARG_API_KEY".
    *   Cleanup: `unset YOUTUBE_API_KEY`

## 3. API Key Handling (in `main` and `fetch_video_info`)

Focuses on how the API key is managed and its necessity.

*   **Test Case 3.1:** `fetch_video_info` called with empty API key
    *   Scenario: Internally call `fetch_video_info("some_id", "")`
    *   Expected: Function returns an empty/error `VideoInfo` struct, logs an error.
*   **Test Case 3.2:** `main` - No API key provided (neither arg nor env)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"` (ensure `YOUTUBE_API_KEY` is unset)
    *   Expected: Error message about missing API key, exits non-zero.
*   **Test Case 3.3:** `main` - API key via `--api-key` argument
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "ARG_KEY"`
    *   Expected: `fetch_video_info` is called with "ARG_KEY".
*   **Test Case 3.4:** `main` - API key via `YOUTUBE_API_KEY` environment variable
    *   Setup: `export YOUTUBE_API_KEY="ENV_KEY"`
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ"`
    *   Expected: `fetch_video_info` is called with "ENV_KEY".
    *   Cleanup: `unset YOUTUBE_API_KEY`

## 4. Filename Sanitization (`sanitize_filename` function)

Tests the behavior of the `sanitize_filename` utility.

*   **Test Case 4.1:** Clean name
    *   Input: `"My Video Title"`
    *   Expected Output: `"My Video Title"`
*   **Test Case 4.2:** Name with slashes
    *   Input: `"A/B/C Video"`
    *   Expected Output: `"A_B_C Video"`
*   **Test Case 4.3:** Name with various invalid characters
    *   Input: `"Video: *Title*? <for_download> | \\official\\"`
    *   Expected Output: `"Video_ _Title__ _for_download_ _ _official_"`
*   **Test Case 4.4:** Empty name
    *   Input: `""`
    *   Expected Output: `""`
*   **Test Case 4.5:** Name with leading/trailing spaces (current implementation does not trim)
    *   Input: `"  My Video  "`
    *   Expected Output: `"  My Video  "` (No change by current sanitizer for this)

## 5. Download Logic (Conceptual due to placeholders)

*   **Test Case 5.1:** Download with a valid, known itag (using placeholder data)
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "KEY" -f "22"`
    *   Expected: `download_video_format` is called. Placeholder file "Rick Astley - Never Gonna Give You Up (Official Music Video)_22.mp4" (or similar based on title) is created in current dir.
*   **Test Case 5.2:** Download with an invalid/unknown itag
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "KEY" -f "9999"`
    *   Expected: Error message "Selected format itag 9999 not found". No download attempted.
*   **Test Case 5.3:** Download with a valid itag but placeholder URL is empty/invalid
    *   Scenario: Modify placeholder data so a format has an empty `url`.
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "KEY" -f "itag_with_empty_url"`
    *   Expected: `download_video_format` called, but logs error "Download URL ... is missing or invalid". Returns false. Main shows download failed.
*   **Test Case 5.4:** Download to a specified output directory
    *   Command: `yt-cli-downloader "dQw4w9WgXcQ" --api-key "KEY" -f "22" -o "/tmp/my_vids"` (Assuming /tmp/my_vids exists and is writable)
    *   Expected: Placeholder file created in `/tmp/my_vids/`.
*   **Test Case 5.5:** Video info fetch fails before download attempt
    *   Scenario: `fetch_video_info` returns an empty `VideoInfo` (e.g. wrong video ID, or API error if it were real).
    *   Command: `yt-cli-downloader "invalid_id" --api-key "KEY" -f "22"`
    *   Expected: Error "Failed to fetch video info." No download attempted.

This list is not exhaustive but covers the main logical paths.
Actual implementation of these tests in a C++ framework would involve
mocking API responses and file system interactions for `fetch_video_info` and
`download_video_format` to make them true unit tests.Tool output for `create_file_with_block`:
