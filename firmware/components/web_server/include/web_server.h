#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t web_server_init(void);
esp_err_t web_server_start(void);
esp_err_t web_server_stop(void);

bool web_server_has_ws_client(void);
