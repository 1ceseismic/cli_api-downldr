cmake_minimum_required(VERSION 3.10)

add_library(yt_core STATIC)

target_sources(yt_core PRIVATE
    core.cpp
    youtube_fetcher.cpp
    yt_wasm_api.cpp  # Add the new API implementation file
)

# This allows #include "core/youtube_fetcher.h" from core.cpp if include dir is project_root/include
target_include_directories(yt_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(yt_core PUBLIC
    nlohmann_json::nlohmann_json
    cpr::cpr # This should now be the cpr built by FetchContent, potentially with Emscripten flags
)

if(YTDLP_CLONE_BUILD_WASM)
  message(STATUS "Configuring yt_core library for Wasm build (Wasm API included)...")
  # Add any Wasm-specific compile definitions or flags for yt_core if necessary
  # target_compile_definitions(yt_core PRIVATE EMSCRIPTEN_BUILD)
else()
  message(STATUS "Configuring yt_core library for native build (Wasm API included)...")
endif()
