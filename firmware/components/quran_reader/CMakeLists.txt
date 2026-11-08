idf_component_register(
    SRCS "quran_reader.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash epaper board_config sdcard log
)
