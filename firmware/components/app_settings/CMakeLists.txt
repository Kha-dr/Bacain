idf_component_register(
    SRCS "app_settings.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES nvs_flash board_config
)