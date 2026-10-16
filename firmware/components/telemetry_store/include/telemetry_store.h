#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define FILE_LIST_MAX_FILES  40
#define FILE_LIST_MAX_NAME   128
#define FILE_LIST_READY_BIT  (1 << 0)

typedef struct {
    float current;
    float target;
} temp_data_t;

typedef struct {
    temp_data_t hotend;
    temp_data_t bed;
    int64_t     updated_at;
} thermal_data_t;

typedef struct {
    float   x;
    float   y;
    float   z;
    float   e;
    int64_t updated_at;
} position_data_t;

typedef enum {
    PRINT_STATE_IDLE = 0,
    PRINT_STATE_PRINTING,
    PRINT_STATE_PAUSED,
    PRINT_STATE_FINISHED,
    PRINT_STATE_ERROR,
} print_state_t;

typedef struct {
    print_state_t state;
    float         progress_pct;
    uint32_t      byte_pos;
    uint32_t      byte_total;
    char          filename[64];
    uint16_t      speed_override;
    uint16_t      flow_override;
    uint8_t       fan_speed;
    int64_t       updated_at;
} print_data_t;

typedef enum {
    COMM_STATE_DISCONNECTED = 0,
    COMM_STATE_CONNECTED,
    COMM_STATE_ERROR,
} comm_state_t;

typedef struct {
    comm_state_t state;
    uint32_t     errors;
    uint32_t     timeouts;
    uint32_t     tx_cmds;
    uint32_t     rx_bytes;
    int32_t      latency_ms;
} comm_stats_t;

typedef struct {
    bool thermal;
    bool position;
    bool print;
    bool comm;
} dirty_flags_t;

esp_err_t telemetry_store_init(void);

void telemetry_store_set_thermal(const thermal_data_t *data);
void telemetry_store_set_position(const position_data_t *data);
void telemetry_store_set_print(const print_data_t *data);
void telemetry_store_set_comm(const comm_stats_t *data);

void telemetry_store_set_filename(const char *filename);
void telemetry_store_set_speed_override(uint16_t pct);
void telemetry_store_set_flow_override(uint16_t pct);
void telemetry_store_set_fan_speed(uint8_t speed);

thermal_data_t  telemetry_store_get_thermal(void);
position_data_t telemetry_store_get_position(void);
print_data_t    telemetry_store_get_print(void);
comm_stats_t    telemetry_store_get_comm(void);

dirty_flags_t telemetry_store_get_and_clear_dirty(void);

void telemetry_store_file_list_begin(void);
void telemetry_store_file_list_add(const char *filename);
void telemetry_store_file_list_end(void);
int  telemetry_store_get_file_list(char out[][FILE_LIST_MAX_NAME], int max_files);
bool telemetry_store_wait_file_list(uint32_t timeout_ms);
