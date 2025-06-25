cmake_minimum_required(VERSION 3.14) # Bump for more robust FetchContent & Emscripten compatibility
project(YouTubeFormatConverter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Option for WebAssembly Build ---
option(YTDLP_CLONE_BUILD_WASM "Build the WebAssembly module" OFF)

# --- Include FetchContent module ---
include(FetchContent)

# --- Dependencies ---
# nlohmann/json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# cxxopts
FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG v3.2.0
)
# cxxopts is only needed for CLI
if(NOT YTDLP_CLONE_BUILD_WASM)
  FetchContent_MakeAvailable(cxxopts)
endif()

# cpr
FetchContent_Declare(
  cpr_dependency # Renamed to avoid conflict with potential find_package(cpr)
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
)

if(YTDLP_CLONE_BUILD_WASM)
  message(STATUS "WASM Build is ON. Configuring for Emscripten.")
  # Emscripten specific CPR options
  set(CPR_USE_SYSTEM_CURL OFF CACHE BOOL "Do not use system Curl" FORCE)
  set(CPR_BUILD_TESTS OFF CACHE BOOL "Disable CPR tests" FORCE)
  set(CPR_BUILD_EXAMPLES OFF CACHE BOOL "Disable CPR examples" FORCE)
  set(CPR_ENABLE_SSL OFF CACHE BOOL "Disable SSL for CPR in Wasm if not strictly needed or causing issues" FORCE) # Example: might simplify build
  # If CPR has specific Emscripten flags, set them here. For example:
  set(EMSCRIPTEN 1 CACHE BOOL "Signal Emscripten build to dependencies" FORCE)

  # Disable CLI build when building for Wasm
  set(YTDLP_CLONE_BUILD_CLI OFF CACHE BOOL "Disable CLI for Wasm build" FORCE)
else()
  message(STATUS "WASM Build is OFF. Configuring for native build.")
  set(YTDLP_CLONE_BUILD_CLI ON CACHE BOOL "Enable CLI for native build" FORCE) # Ensure CLI is ON for native
endif()

FetchContent_MakeAvailable(cpr_dependency) # cpr target should be available as cpr::cpr

# --- Subdirectories ---
# Core library is always added as it's needed for both CLI and Wasm
add_subdirectory(src/core)

# CLI subdirectory, conditionally added
if(YTDLP_CLONE_BUILD_CLI AND NOT YTDLP_CLONE_BUILD_WASM)
  add_subdirectory(src/cli)
  message(STATUS "CLI build is enabled.")
else()
  message(STATUS "CLI build is disabled (either Wasm build is ON or YTDLP_CLONE_BUILD_CLI is OFF).")
endif()


# --- WebAssembly Specific Configuration ---
if(YTDLP_CLONE_BUILD_WASM)
  set(EMSDK_OUTPUT_BASENAME "yt_wasm_module") # Base name for .js and .wasm files

  # Define exported functions for Wasm module (note the leading underscore)
  set(WASM_EXPORTED_FUNCTIONS
    "_get_video_info_json"
    "_free_c_string"
    "_get_stream_url_json"  # Add new function
  )
  # Create a comma-separated string for Emscripten flags
  string(JOIN "," WASM_EXPORTED_FUNCTIONS_STR "${WASM_EXPORTED_FUNCTIONS}")


  # Emscripten Linker Flags
  set(CMAKE_EXE_LINKER_FLAGS
    "-s WASM=1"
    "-s MODULARIZE=1"               # Create a modularized JS file
    "-s EXPORT_ES6=0"               # Set to 1 for ES6 module, 0 for traditional
    "-s EXPORT_NAME='ytWasmModule'" # Name of the module instance if not ES6
    "-s ALLOW_MEMORY_GROWTH=1"
    "-s STACK_SIZE=5MB"             # Example stack size
    "-s INITIAL_MEMORY=32MB"        # Example initial memory
    "-s EXPORTED_FUNCTIONS=${WASM_EXPORTED_FUNCTIONS_STR}"
    "-s EXPORTED_RUNTIME_METHODS=['ccall', 'cwrap', 'UTF8ToString', 'stringToUTF8']" # Useful runtime methods
    "-s FETCH=1"                    # Enable Emscripten fetch support for cpr/curl
    "--no-entry"                    # Since we are building a library/module, not an app with main()
    "-o ${CMAKE_BINARY_DIR}/${EMSDK_OUTPUT_BASENAME}.html" # Output .js and .wasm, and an example html
  )
  # Add any other necessary Emscripten flags (e.g., for pthreads, SIMD, etc. if used)
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread -s PROXY_TO_PTHREAD") # If using pthreads

  add_executable(${EMSDK_OUTPUT_BASENAME} src/core/dummy_main_for_wasm.cpp)

  target_link_libraries(${EMSDK_OUTPUT_BASENAME} PUBLIC yt_core) # Link against our core library

  message(STATUS "Wasm module target: ${EMSDK_OUTPUT_BASENAME}")
  message(STATUS "Exported functions: ${WASM_EXPORTED_FUNCTIONS_STR}")
  message(STATUS "Output files will be: ${CMAKE_BINARY_DIR}/${EMSDK_OUTPUT_BASENAME}.js, ${CMAKE_BINARY_DIR}/${EMSDK_OUTPUT_BASENAME}.wasm")
endif()

# --- Include directories (for project's own headers) ---
include_directories(include)

# --- Message Status ---
message(STATUS "Project Name: ${PROJECT_NAME}")
message(STATUS "Project Version: ${PROJECT_VERSION}")
message(STATUS "CMake Version: ${CMAKE_VERSION}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}") # Often set by user e.g. -DCMAKE_BUILD_TYPE=Release
message(STATUS "Build WASM: ${YTDLP_CLONE_BUILD_WASM}")
message(STATUS "Build CLI: ${YTDLP_CLONE_BUILD_CLI}")
