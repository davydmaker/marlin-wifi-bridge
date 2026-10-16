idf_component_register(
    SRCS "power_manager.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log esp_timer freertos telemetry_store wifi_manager web_server cmd_scheduler cmd_dispatcher marlin_comm settings_store
)
