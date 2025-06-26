project('yt-cli-downloader', 'cpp', # Assuming no other C files, 'c' can be removed
        version : '0.1.0',
        default_options : ['cpp_std=c++17', 'warning_level=2'])

# --- Dependencies ---
# CPR (cpr::cpr)
cpr_dep = dependency('cpr', version : '>=1.9', required : true)

# nlohmann_json (nlohmann_json::nlohmann_json)
nlohmann_json_dep = dependency('nlohmann_json', version : '>=3.10', required : true)

# --- Include Directories ---
# Only 'src' is needed now if yt_sig_decipher.hpp was in src
inc_dirs = include_directories('src')

# --- Source Files ---
executable_sources = [
    'src/yt_cli_downloader.cpp'
    # Removed: 'src/yt_sig_decipher.cpp'
    # Removed: 'vendor/duktape/duktape.c'
]

# --- Executable Definition ---
executable('yt-cli-downloader',
           executable_sources,
           include_directories : inc_dirs,
           dependencies : [cpr_dep, nlohmann_json_dep],
           cpp_args : ['-pthread'], # For CPR, if it requires pthreads
           install : true)

# Any previous sections related to building Duktape as a separate library
# or a duktape_dep should also be removed.
