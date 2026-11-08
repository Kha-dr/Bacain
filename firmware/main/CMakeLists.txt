idf_component_register(
    SRCS
        "main.c"
        "reader_ui.c"
        "views/view_reading.c"
        "views/view_menu.c"
        "views/view_library.c"
        "views/view_ble_sync.c"
        "views/view_bookmarks.c"
        "views/view_nav_hints.c"
        "views/view_mushaf.c"
    INCLUDE_DIRS
        "."
        "views"
    REQUIRES
        nvs_flash
        freertos
        esp_timer
        log
        driver
        esp_driver_gpio
        power_mgr
        sdcard
        button
        epaper
        reader_core
        ble_sync
        status_led
        mushaf_reader
)

idf_build_get_property(python_cmd PYTHON)
if(NOT python_cmd)
    set(python_cmd "python")
endif()

set(QRCODE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/sync_qrcode.h")
set(QRCODE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/generate_qrcode.py")

add_custom_command(
    OUTPUT "${QRCODE_HEADER}"
    COMMAND ${python_cmd} "${QRCODE_SCRIPT}" --output "${QRCODE_HEADER}"
    DEPENDS "${QRCODE_SCRIPT}"
    COMMENT "Generating sync_qrcode.h"
    VERBATIM
)

add_custom_target(generate_sync_qrcode DEPENDS "${QRCODE_HEADER}")
add_dependencies(${COMPONENT_LIB} generate_sync_qrcode)
