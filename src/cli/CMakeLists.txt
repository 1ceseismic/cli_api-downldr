cmake_minimum_required(VERSION 3.10)

# --- Executable Definition ---
if(YTDLP_CLONE_BUILD_CLI)
    add_executable(yt_cli main.cpp) # Added main.cpp directly here

    # --- Link Libraries ---
    target_link_libraries(yt_cli PRIVATE yt_core)
    target_link_libraries(yt_cli PRIVATE cxxopts::cxxopts)

    # --- Message ---
    message(STATUS "Configuring yt_cli executable...")
else()
    message(STATUS "Skipping CLI build as YTDLP_CLONE_BUILD_CLI is OFF.")
endif()
