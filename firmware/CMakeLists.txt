cmake_minimum_required(VERSION 3.16)

# Force ESP32-S3 target in environment so child subprojects (bootloader) inherit it
set(ENV{IDF_TARGET} "esp32s3")

# Minimal ESP-IDF Project Configuration for BacaIn
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bacain)
