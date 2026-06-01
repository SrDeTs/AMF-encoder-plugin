cmake_minimum_required(VERSION 3.20)
project(amf_encoder_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BASE_DIR "${CMAKE_SOURCE_DIR}")
set(FFMPEG_DIR "${BASE_DIR}/ffmpeg")
set(WRAPPER_DIR "${BASE_DIR}/wrapper")
set(LINUX_BUNDLE_DIR "${CMAKE_BINARY_DIR}/amf_encoder_plugin.dvcp.bundle/Contents/Linux-x86-64")
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "-fPIC -Wall -Wno-unused-variable -Wno-multichar")
endif ()

find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVUTIL_LIBRARY avutil)
find_library(SWSCALE_LIBRARY swscale)

if (NOT FFMPEG_INCLUDE_DIR OR NOT AVCODEC_LIBRARY OR NOT AVUTIL_LIBRARY OR NOT SWSCALE_LIBRARY)
    if (EXISTS "${FFMPEG_DIR}/include/libavcodec/avcodec.h" AND
        EXISTS "${FFMPEG_DIR}/lib/libavcodec.a" AND
        EXISTS "${FFMPEG_DIR}/lib/libavutil.a" AND
        EXISTS "${FFMPEG_DIR}/lib/libswscale.a")
        set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
        set(AVCODEC_LIBRARY "${FFMPEG_DIR}/lib/libavcodec.a")
        set(AVUTIL_LIBRARY "${FFMPEG_DIR}/lib/libavutil.a")
        set(SWSCALE_LIBRARY "${FFMPEG_DIR}/lib/libswscale.a")
    endif ()

message(STATUS "FFmpeg include dir: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "FFmpeg avcodec library: ${AVCODEC_LIBRARY}")
message(STATUS "FFmpeg avutil library: ${AVUTIL_LIBRARY}")
message(STATUS "FFmpeg swscale library: ${SWSCALE_LIBRARY}")

get_filename_component(AVCODEC_EXT "${AVCODEC_LIBRARY}" EXT)
if (AVCODEC_EXT STREQUAL ".a")
    set(FFMPEG_IS_STATIC TRUE)
    find_package(PkgConfig REQUIRED)

    execute_process(
        COMMAND /usr/bin/env PKG_CONFIG_PATH=${FFMPEG_DIR}/lib/pkgconfig pkg-config --libs --static libavcodec libavutil libswscale
        OUTPUT_VARIABLE FFMPEG_STATIC_LIBS_RAW
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    separate_arguments(FFMPEG_STATIC_LIBS NATIVE_COMMAND "${FFMPEG_STATIC_LIBS_RAW}")
    message(STATUS "Using static FFmpeg toolchain from ${FFMPEG_DIR}")
else ()
    message(WARNING "Using shared FFmpeg libraries from the system. The resulting plugin will be much smaller than the GitHub release artifact.")
endif ()

include_directories(include wrapper ${FFMPEG_INCLUDE_DIR})

set(SRCS
        av1_amf_encoder.cpp
        ffmpeg_encoder.cpp
        h264_amf_encoder.cpp
        h265_amf_encoder.cpp
        plugin.cpp
        uisettings_controller.cpp
)

set(HEADERS
        av1_amf_encoder.h
        ffmpeg_encoder.h
        h264_amf_encoder.h
        h265_amf_encoder.h
        plugin.h
        uisettings_controller.h
)

add_subdirectory(${WRAPPER_DIR})

add_library(${PROJECT_NAME} SHARED ${SRCS} ${HEADERS})

target_compile_definitions(${PROJECT_NAME} PRIVATE __STDC_CONSTANT_MACROS)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".dvcp")
set_target_properties(${PROJECT_NAME} PROPERTIES BUILD_RPATH "\$ORIGIN")
set_target_properties(${PROJECT_NAME} PROPERTIES INSTALL_RPATH "\$ORIGIN")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(${PROJECT_NAME} PRIVATE -shared -static-libstdc++ -static-libgcc -Wl,--no-undefined -Wl,-Bsymbolic)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${LINUX_BUNDLE_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${LINUX_BUNDLE_DIR}/"
            COMMAND ${CMAKE_STRIP} --strip-unneeded "$<TARGET_FILE:${PROJECT_NAME}>"
            COMMAND ${CMAKE_STRIP} --strip-unneeded "${LINUX_BUNDLE_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM)
else ()
    target_link_options(${PROJECT_NAME} PRIVATE -dynamiclib)
endif ()

target_link_libraries(${PROJECT_NAME} PRIVATE host_plugin)

if (FFMPEG_IS_STATIC)
    target_link_directories(${PROJECT_NAME} PRIVATE "${FFMPEG_DIR}/lib")

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(${PROJECT_NAME} PRIVATE
                -Wl,--start-group
                ${AVCODEC_LIBRARY}
                ${SWSCALE_LIBRARY}
                ${AVUTIL_LIBRARY}
                ${FFMPEG_STATIC_LIBS}
                -Wl,--end-group
                numa)
    else ()
        target_link_libraries(${PROJECT_NAME} PRIVATE
                ${AVCODEC_LIBRARY}
                ${SWSCALE_LIBRARY}
                ${AVUTIL_LIBRARY}
                ${FFMPEG_STATIC_LIBS})
    endif ()

else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${AVCODEC_LIBRARY} ${AVUTIL_LIBRARY} ${SWSCALE_LIBRARY})
endif ()
