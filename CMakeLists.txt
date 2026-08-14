cmake_minimum_required(VERSION 3.20)

project(amf_encoder_plugin VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(AMF_INCLUDE_DIR NAMES AMF/core/Factory.h REQUIRED)
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
    h264_config.cpp
    h264_config.h
    h264_amf_encoder.cpp
    h264_amf_encoder.h
    h265_amf_encoder.cpp
    h265_amf_encoder.h
    hevc_config.cpp
    hevc_config.h
    plugin.cpp
    plugin.h
    rgb16_to_p010.cpp
    rgb16_to_p010.h
)

target_compile_definitions(amf_encoder_plugin PRIVATE __STDC_CONSTANT_MACROS)
target_compile_options(amf_encoder_plugin PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-unused-variable)
target_include_directories(amf_encoder_plugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/wrapper"
)
target_include_directories(amf_encoder_plugin SYSTEM PRIVATE "${AMF_INCLUDE_DIR}")
target_link_libraries(amf_encoder_plugin PRIVATE host_plugin "${AMF_RUNTIME_LIBRARY}")

add_executable(amf_av1_crop_fix
    tools/amf_av1_crop_fix.cpp
    av1_container_fix.cpp
    av1_container_fix.h
)
target_include_directories(amf_av1_crop_fix PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(amf_av1_crop_fix PRIVATE -Wall -Wextra -Wpedantic)

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

if (BUILD_TESTING)
    add_executable(rgb16_to_p010_test
        tests/rgb16_to_p010_test.cpp
        rgb16_to_p010.cpp
    )
    target_include_directories(rgb16_to_p010_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(rgb16_to_p010_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME rgb16_to_p010 COMMAND rgb16_to_p010_test)

    add_executable(hevc_config_test
        tests/hevc_config_test.cpp
        hevc_config.cpp
    )
    target_include_directories(hevc_config_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(hevc_config_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME hevc_config COMMAND hevc_config_test)

    add_executable(h264_config_test
        tests/h264_config_test.cpp
        h264_config.cpp
    )
    target_include_directories(h264_config_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(h264_config_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME h264_config COMMAND h264_config_test)

    add_executable(av1_container_fix_test
        tests/av1_container_fix_test.cpp
        av1_container_fix.cpp
    )
    target_include_directories(av1_container_fix_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(av1_container_fix_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME av1_container_fix COMMAND av1_container_fix_test)
endif ()
