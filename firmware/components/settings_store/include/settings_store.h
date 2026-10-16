#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint16_t wifi_hold_s;
    uint16_t standby_timeout_s;
    uint16_t dim_timeout_s;
    uint16_t poll_interval_ms;
    uint16_t cmd_timeout_ms;
    uint8_t  cmd_max_retries;
} settings_t;

esp_err_t settings_store_init(void);

settings_t settings_store_get(void);

esp_err_t settings_store_save(const settings_t *s);
