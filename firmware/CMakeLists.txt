cmake_minimum_required(VERSION 3.16)

# Force ESP32-S3 target in environment so child subprojects (bootloader) inherit it
set(ENV{IDF_TARGET} "esp32s3")

# Minimal ESP-IDF Project Configuration for BacaIn
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Forward any -DBOARD_* or -DCONFIG_BOARD_* CMake cache variables as BOARD_* compile definitions
# (stripping CONFIG_ avoids -Werror macro redefinition conflicts with build/config/sdkconfig.h)
include_directories("${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/boards")
get_cmake_property(_cache_vars CACHE_VARIABLES)
foreach(_var ${_cache_vars})
    if(_var MATCHES "^(BOARD_|CONFIG_BOARD_)")
        get_property(_type CACHE ${_var} PROPERTY TYPE)
        if(NOT _type STREQUAL "INTERNAL" AND NOT _type STREQUAL "STATIC")
            string(REGEX REPLACE "^CONFIG_" "" _board_macro "${_var}")
            set(_board_val "${${_var}}")
            if(_board_val STREQUAL "y" OR _board_val STREQUAL "Y")
                set(_board_val "1")
            elseif(_board_val STREQUAL "n" OR _board_val STREQUAL "N")
                set(_board_val "0")
            endif()
            if(_board_macro MATCHES "^BOARD_PROFILE_(.+)$" AND _board_val STREQUAL "1")
                string(TOLOWER "${CMAKE_MATCH_1}" _profile_id)
                if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${_profile_id}/board_def.h")
                    include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}/boards/${_profile_id}")
                    add_compile_definitions(BOARD_PROFILE_HAS_DEF_H=1)
                endif()
            endif()
            add_compile_definitions(${_board_macro}=${_board_val})
        endif()
    endif()
endforeach()

project(bacain)
