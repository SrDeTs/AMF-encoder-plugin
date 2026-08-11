add_library(host_plugin STATIC
    host_api.cpp
    host_api.h
    plugin_api.cpp
    plugin_api.h
)

target_compile_features(host_plugin PUBLIC cxx_std_20)
target_include_directories(host_plugin PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)
