#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DISPLAY_PAGE_STATUS = 0,
    DISPLAY_PAGE_CONFIG,
    DISPLAY_PAGE_MAX,
} display_page_t;

esp_err_t display_init(void);
esp_err_t display_start(void);

void display_next_page(void);

void display_set_page(display_page_t page);

void display_set_brightness(uint8_t pct);

void display_set_backlight_override(bool always_on);

void display_wake(void);
