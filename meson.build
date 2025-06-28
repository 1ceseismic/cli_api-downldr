project('yt-cli-downloader', 'cpp', # Assuming no other C files, 'c' can be removed
        version : '0.1.0',
        default_options : ['cpp_std=c++17', 'warning_level=2'])


cpr_dep = dependency('cpr', version : '>=1.9', required : true)


nlohmann_json_dep = dependency('nlohmann_json', version : '>=3.10', required : true)

inc_dirs = include_directories('src')


executable_sources = [
    'src/yt_cli_downloader.cpp'

]


executable('yt-cli-downloader',
           executable_sources,
           include_directories : inc_dirs,
           dependencies : [cpr_dep, nlohmann_json_dep],
           cpp_args : ['-pthread'], 
           install : true)

