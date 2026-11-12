idf_component_register(
    SRCS "text_source.c" "text_layout.c"
    INCLUDE_DIRS "."
    REQUIRES board_config
)