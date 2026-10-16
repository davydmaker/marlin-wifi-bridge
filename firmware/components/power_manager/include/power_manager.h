#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    POWER_STANDBY = 0,
    POWER_ACTIVE,
} power_state_t;

esp_err_t power_manager_init(void);

esp_err_t power_manager_start(void);

void power_manager_request_active(void);

power_state_t power_manager_get_state(void);

bool power_manager_is_activating(void);
