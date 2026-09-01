cmake_minimum_required(VERSION 3.16)

add_definitions(-DLV_LVGL_H_INCLUDE_SIMPLE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AroNet)

# ESP-IDF 6.0 promotes stringop-truncation/overflow to -Werror by default
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-truncation" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=stringop-overflow" APPEND)

# Custom partition table for 16MB flash with OTA
set(PARTITION_TABLE_CUSTOM_FILENAME "esp32s3-16mb.csv")
