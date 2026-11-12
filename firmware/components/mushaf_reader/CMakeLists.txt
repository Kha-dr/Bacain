idf_component_register(
    SRCS "mushaf_reader.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash epaper board_config sdcard log reader_core
)
