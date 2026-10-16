#pragma once

#include "esp_err.h"

typedef enum {
    BUTTON_EVT_SHORT_PRESS = 0,
    BUTTON_EVT_LONG_PRESS,
    BUTTON_EVT_VERY_LONG_PRESS,
} button_event_t;

typedef void (*button_callback_t)(button_event_t event);

esp_err_t button_init(button_callback_t cb);

esp_err_t button_start(void);
