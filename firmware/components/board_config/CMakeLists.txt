set(board_srcs "")
if(DEFINED BOARD_PROFILE)
    string(TOLOWER "${BOARD_PROFILE}" _profile_id)
    set(_board_c "${CMAKE_CURRENT_LIST_DIR}/../../boards/${_profile_id}/board.c")
    if(EXISTS "${_board_c}")
        set(board_srcs "${_board_c}")
    endif()
endif()

idf_component_register(
    SRCS ${board_srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_common driver
)

if(board_srcs)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-u board_epd_get_lut_partial -u board_power_pre_sleep -u board_light_sleep_pins_init")
endif()

