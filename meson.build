project('yt-cli-downloader', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17'])

# --- Dependencies ---
# These would be needed for actual API calls.
# You'd typically install these libraries (e.g., libcurl-dev, nlohmann-json-dev)
# or use Meson's subproject or wrap features to manage them.

# Example for nlohmann/json (if installed system-wide or via pkg-config)
# json_dep = dependency('nlohmann_json', required : true)
# Or, using a subproject (assuming nlohmann/json is added as a subproject in 'subprojects' dir):
# json_proj = subproject('nlohmann_json')
# json_dep = json_proj.get_variable('nlohmann_json_dep')

# Example for cpr (libcurl wrapper) - often used as a subproject or header-only with system libcurl
# cpr_dep = dependency('cpr', required : true) # If cpr provides a .pc file
# curl_dep = dependency('libcurl', required : true) # cpr depends on libcurl

# For now, since we are not making actual calls, these are commented out.
# When implementing, uncomment and configure according to how you manage dependencies.

cpp_args = []
link_args = []
deps = []

# Placeholder for adding actual dependencies to the executable
# if json_dep.found() and cpr_dep.found() and curl_dep.found()
#   deps += [json_dep, cpr_dep, curl_dep]
# else
#   warning('One or more dependencies (json, cpr, curl) not found. API calls will not work.')
# endif

executable('yt-cli-downloader',
           'yt_cli_downloader.cpp',
           dependencies : deps, # Add dependencies here
           cpp_args : cpp_args,
           link_args : link_args,
           install : true)
