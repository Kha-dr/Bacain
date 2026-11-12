idf_component_register(
    SRCS "mushaf_reader.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash epaper board_config sdcard log reader_core text_engine
    PRIV_REQUIRES ui_kit
)