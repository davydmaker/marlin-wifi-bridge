#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *gcode;
    uint32_t    interval_ms;
    uint8_t     priority;
    bool        enabled;
    bool        only_when_printing;
} poll_entry_t;

esp_err_t cmd_scheduler_init(void);
esp_err_t cmd_scheduler_start(void);

void cmd_scheduler_pause(bool paused);

esp_err_t cmd_scheduler_set_enabled(const char *gcode, bool enabled);

esp_err_t cmd_scheduler_set_interval(const char *gcode, uint32_t interval_ms);
