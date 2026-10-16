idf_component_register(
    SRCS "marlin_comm.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log usb espressif__usb_host_cdc_acm espressif__usb_host_ch34x_vcp marlin_parser telemetry_store esp_timer freertos
)
