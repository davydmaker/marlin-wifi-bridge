idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log esp_http_server json telemetry_store cmd_dispatcher wifi_manager marlin_comm esp_timer freertos driver
)
