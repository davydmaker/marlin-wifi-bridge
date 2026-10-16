idf_component_register(
    SRCS "config_portal.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_http_server wifi_manager settings_store nvs_flash log esp_timer freertos
)
