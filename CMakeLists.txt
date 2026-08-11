cmake_minimum_required(VERSION 3.20)

project(amf_encoder_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(AMF_RUNTIME_LIBRARY NAMES amfrt64 REQUIRED)

set(PLUGIN_BUNDLE_DIR "${CMAKE_BINARY_DIR}/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64")

add_subdirectory(wrapper)

add_library(amf_encoder_plugin SHARED
    amf_encoder.cpp
    amf_encoder.h
    amf_settings.cpp
    amf_settings.h
    av1_amf_encoder.cpp
    av1_amf_encoder.h
    encoder_info.h
    h264_amf_encoder.cpp
    h264_amf_encoder.h
    plugin.cpp
    plugin.h
)

target_compile_definitions(amf_encoder_plugin PRIVATE __STDC_CONSTANT_MACROS)
target_compile_options(amf_encoder_plugin PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)
target_include_directories(amf_encoder_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/wrapper"
)
target_include_directories(amf_encoder_plugin SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(amf_encoder_plugin PRIVATE host_plugin "${AMF_RUNTIME_LIBRARY}")

set_target_properties(amf_encoder_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".dvcp"
    BUILD_RPATH "\$ORIGIN"
    INSTALL_RPATH "\$ORIGIN"
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(amf_encoder_plugin PRIVATE -Wl,--no-undefined -Wl,-Bsymbolic)
    add_custom_command(TARGET amf_encoder_plugin POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${PLUGIN_BUNDLE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:amf_encoder_plugin>"
            "${PLUGIN_BUNDLE_DIR}/"
        COMMAND "${CMAKE_STRIP}" --strip-unneeded "$<TARGET_FILE:amf_encoder_plugin>"
        COMMAND "${CMAKE_STRIP}" --strip-unneeded
            "${PLUGIN_BUNDLE_DIR}/$<TARGET_FILE_NAME:amf_encoder_plugin>"
        VERBATIM
    )
endif ()
