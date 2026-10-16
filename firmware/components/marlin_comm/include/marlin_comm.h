#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>
#include <stdint.h>

#define MARLIN_CMD_MAX_LEN  80
#define MARLIN_TX_QUEUE_LEN 12

typedef struct {
    char     cmd[MARLIN_CMD_MAX_LEN];
    uint8_t  priority;
    int64_t  enqueued_at;
} marlin_cmd_t;

typedef enum {
    MARLIN_COMM_DISCONNECTED = 0,
    MARLIN_COMM_CONNECTED,
    MARLIN_COMM_IDLE,
    MARLIN_COMM_WAITING_OK,
    MARLIN_COMM_ERROR,
} marlin_comm_state_t;

esp_err_t marlin_comm_init(void);
esp_err_t marlin_comm_start(void);

esp_err_t marlin_comm_send(const char *gcode, uint8_t priority);

marlin_comm_state_t marlin_comm_get_state(void);

QueueHandle_t marlin_comm_get_tx_queue(void);

void     marlin_comm_set_poll_interval(uint32_t ms);
uint32_t marlin_comm_get_poll_interval(void);

void marlin_comm_set_cmd_timeout(uint32_t ms);
void marlin_comm_set_cmd_max_retries(uint8_t retries);
