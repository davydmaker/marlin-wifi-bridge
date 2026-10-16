idf_component_register(
    SRCS "button.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver freertos log esp_timer
)
