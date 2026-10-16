idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        button
        config_portal
        display
        marlin_comm
        marlin_parser
        power_manager
        settings_store
        telemetry_store
        wifi_manager
        nvs_flash
        log
)
