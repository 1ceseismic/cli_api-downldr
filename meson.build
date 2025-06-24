project('yt-cli-downloader', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17'])

# --- Dependencies ---
# nlohmann/json: Often used as a header-only library or via pkg-config if installed.
# For header-only, you might just need to ensure it's in the include path.
# If it provides a pkg-config file (e.g., nlohmann_json.pc), dependency() works.
# If you add it as a subproject (e.g., in subprojects/nlohmann_json), you'd use:
#   json_proj = subproject('nlohmann_json')
#   json_dep = json_proj.get_variable('nlohmann_json_dep')
# Assuming system/pkg-config for simplicity here:
json_dep = dependency('nlohmann_json', version: '>=3.0.0', required: true)

# cpr (C++ Requests): Typically depends on libcurl.
# Similar to json, can be system-installed (with pkg-config for cpr and libcurl)
# or included as a subproject.
# If cpr is built as a static/shared lib and installed with a .pc file:
cpr_dep = dependency('cpr', version: '>=1.6.0', required: true) # cpr itself
# libcurl is a common dependency for cpr. Often cpr's own build system or pkg-config handles linking it.
# If cpr's .pc file doesn't list libcurl as a requirement, or if you link cpr manually,
# you might need to declare libcurl explicitly:
# curl_dep = dependency('libcurl', required: true)
# However, a well-packaged cpr should handle this.

# If cpr is used as header-only but still needs libcurl linked:
# curl_dep = dependency('libcurl', required: true)
# And cpr headers would need to be in include_directories.
# For this example, we assume cpr is a library found by pkg-config.

cpp_args = []
link_args = []
# Consolidate dependencies
# If curl_dep was explicitly needed, add it here: deps = [json_dep, cpr_dep, curl_dep]
deps = [json_dep, cpr_dep]


executable('yt-cli-downloader',
           'yt_cli_downloader.cpp',
           dependencies : deps,
           cpp_args : cpp_args,
           link_args : link_args,
           install : true)
