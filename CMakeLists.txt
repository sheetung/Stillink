cmake_minimum_required(VERSION 3.16.0)

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/main/version.h" STILLINK_VERSION_LINE
     REGEX "^#define EPAPER_VERSION[ ]+")
string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" PROJECT_VER "${STILLINK_VERSION_LINE}")

set(IDF_TARGET "esp32s3" CACHE STRING "Stillink target chip")
if(NOT IDF_TARGET STREQUAL "esp32s3" AND NOT IDF_TARGET STREQUAL "esp32c3")
    message(FATAL_ERROR "Stillink supports ESP32-S3 and ESP32-C3")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(stillink)
