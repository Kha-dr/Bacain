idf_component_register(
    SRCS "ui_display.c" "ui_text.c" "ui_widgets.c" "ui_list.c" "ui_pager.c"
    INCLUDE_DIRS "."
    REQUIRES epaper board_config button text_engine   # in public headers
    PRIV_REQUIRES power_mgr                           # ui_widgets.c (battery)
)