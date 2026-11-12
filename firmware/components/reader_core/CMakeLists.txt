idf_component_register(
    SRCS
        "book_library.c"
        "text_pager.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        epaper
        sdcard
        log
        freertos
        text_engine     # text_pager.h: text_layout.h
        ui_kit          # text_pager.h: ui_pager.h
)