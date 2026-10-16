#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MARLIN_RESP_OK,
    MARLIN_RESP_ERROR,
    MARLIN_RESP_TEMP,
    MARLIN_RESP_POSITION,
    MARLIN_RESP_SD_PROGRESS,
    MARLIN_RESP_FILE_LIST,
    MARLIN_RESP_ECHO,
    MARLIN_RESP_BUSY,
    MARLIN_RESP_UNKNOWN,
} marlin_resp_type_t;

esp_err_t marlin_parser_init(void);

marlin_resp_type_t marlin_parser_process_line(const char *line);

bool marlin_parser_is_terminal(marlin_resp_type_t type);
