cmake_minimum_required(VERSION 3.16)

# Force ESP32-S3 target in environment so child subprojects (bootloader) inherit it
set(ENV{IDF_TARGET} "esp32s3")

# Minimal ESP-IDF Project Configuration for BacaIn
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Forward any -DBOARD_* or -DCONFIG_BOARD_* CMake cache variables as C/C++ compile definitions
get_cmake_property(_cache_vars CACHE_VARIABLES)
foreach(_var ${_cache_vars})
    if(_var MATCHES "^(BOARD_|CONFIG_BOARD_)")
        get_property(_type CACHE ${_var} PROPERTY TYPE)
        if(NOT _type STREQUAL "INTERNAL" AND NOT _type STREQUAL "STATIC")
            add_compile_definitions(${_var}=${${_var}})
        endif()
    endif()
endforeach()

project(bacain)
