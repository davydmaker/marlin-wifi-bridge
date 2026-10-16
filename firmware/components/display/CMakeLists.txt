idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log driver esp_lcd espressif__esp_lcd_gc9107 telemetry_store wifi_manager marlin_comm web_server power_manager settings_store esp_timer freertos
)
