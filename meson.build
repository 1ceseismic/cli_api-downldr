project('yt-cli-downloader', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++17'])

# --- Subproject Dependencies ---

# nlohmann/json (header-only, fetched via wrap file)
# The subproject 'nlohmann_json' (directory name from .wrap) should have a meson.build
# that defines the variable 'nlohmann_json_dep' (from [provide] section of .wrap).
json_sub = subproject('nlohmann_json')
json_dep = json_sub.get_variable('nlohmann_json_dep')

# CPR (C++ Requests, fetched via wrap file)
# The subproject 'cpr' (directory name from .wrap) should have a meson.build
# that defines the variable 'cpr_dep' (from [provide] section of .wrap).
# CPR's meson build should also handle its own dependency on libcurl.
# We can pass options to subprojects if needed, for example, to encourage system curl usage
# or disable tests for faster configuration.
cpr_sub = subproject('cpr',
  default_options : [
    'CPR_BUILD_TESTS=disabled', # Don't build CPR's own tests
    'CPR_GENERATE_COVERAGE=false', # Disable coverage for CPR
    'CPR_USE_SYSTEM_CURL=true' # This can be an option if CPR's meson.build supports it explicitly
                               # and you want to ensure it doesn't try to build its own curl.
                               # Usually, CPR's meson will try `dependency('curl')` which is good.
  ]
)
cpr_dep = cpr_sub.get_variable('cpr_dep')


cpp_args = []
link_args = []
deps = [json_dep, cpr_dep]


executable('yt-cli-downloader',
           'yt_cli_downloader.cpp',
           dependencies : deps,
           cpp_args : cpp_args,
           link_args : link_args,
           install : true)
