idf_component_register(
    SRCS "cmd_scheduler.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log marlin_comm esp_timer freertos
)
