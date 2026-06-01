cmake_minimum_required(VERSION 3.20)
project(host_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BASE_DIR "${CMAKE_SOURCE_DIR}/../")

if (NOT MSVC)
    set(CMAKE_CXX_FLAGS "-fPIC -Wall -Wno-unused-variable -Wno-multichar")
endif ()

include_directories(${BASE_DIR}/include)

add_library(${PROJECT_NAME} STATIC host_api.cpp host_api.h plugin_api.cpp plugin_api.h)
