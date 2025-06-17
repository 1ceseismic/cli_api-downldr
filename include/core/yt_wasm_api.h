#ifndef YT_WASM_API_H
#define YT_WASM_API_H

// Standard C interface, usable by Emscripten
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fetches video information for a given YouTube URL and returns it as a JSON string.
 *
 * The JSON string will have the following structure:
 * On success: {"success": true, "data": { ...VideoDetails... }}
 * On failure: {"success": false, "error": "Error message"}
 *
 * The caller is responsible for freeing the returned string using free_c_string().
 *
 * @param video_url_c_str A C-string representing the YouTube video URL.
 * @return A C-string containing the JSON data, or an error JSON. Needs to be freed by free_c_string().
 */
const char* get_video_info_json(const char* video_url_c_str);

/**
 * @brief Frees a C-string that was allocated by the Wasm module (e.g., by get_video_info_json).
 *
 * @param str_ptr Pointer to the C-string to be freed.
 */
void free_c_string(char* str_ptr);

/**
 * @brief Fetches video information for a given YouTube URL and a specific itag,
 *        then returns a JSON string containing the stream's URL and a suggested filename.
 *
 * The JSON string will have the following structure:
 * On success: {"success": true, "url": "stream_url", "suggested_filename": "filename.ext"}
 * On failure: {"success": false, "error": "Error message"}
 *
 * The caller is responsible for freeing the returned string using free_c_string().
 *
 * @param video_url_c_str A C-string representing the YouTube video URL.
 * @param itag The itag of the desired stream.
 * @return A C-string containing the JSON data, or an error JSON. Needs to be freed by free_c_string().
 */
const char* get_stream_url_json(const char* video_url_c_str, int itag);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // YT_WASM_API_H
