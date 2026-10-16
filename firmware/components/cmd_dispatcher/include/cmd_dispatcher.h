#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    CMD_RESULT_OK,
    CMD_RESULT_ERROR,
    CMD_RESULT_BLOCKED,
    CMD_RESULT_QUEUE_FULL,
} cmd_result_t;

esp_err_t cmd_dispatcher_init(void);

cmd_result_t cmd_dispatcher_send(const char *gcode, bool confirmed);

bool cmd_dispatcher_needs_confirm(const char *gcode);
