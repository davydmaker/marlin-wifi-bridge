idf_component_register(
    SRCS "telemetry_store.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log esp_timer freertos
)
