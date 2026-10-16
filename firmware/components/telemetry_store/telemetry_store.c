#include "telemetry_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "telem_store";

static SemaphoreHandle_t s_mutex;
static thermal_data_t    s_thermal;
static position_data_t   s_position;
static print_data_t      s_print;
static comm_stats_t      s_comm;
static dirty_flags_t     s_dirty;

static char s_file_list[FILE_LIST_MAX_FILES][FILE_LIST_MAX_NAME];
static int  s_file_count = 0;
static int  s_file_list_building = 0;
static EventGroupHandle_t s_file_list_event;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

esp_err_t telemetry_store_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_thermal, 0, sizeof(s_thermal));
    memset(&s_position, 0, sizeof(s_position));
    memset(&s_print, 0, sizeof(s_print));
    memset(&s_comm, 0, sizeof(s_comm));
    memset(&s_dirty, 0, sizeof(s_dirty));

    s_print.state = PRINT_STATE_IDLE;
    s_print.speed_override = 100;
    s_print.flow_override = 100;
    s_comm.state = COMM_STATE_DISCONNECTED;

    s_file_list_event = xEventGroupCreate();
    if (!s_file_list_event) {
        ESP_LOGE(TAG, "Failed to create file list event group");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Telemetry store initialized");
    return ESP_OK;
}

void telemetry_store_set_thermal(const thermal_data_t *data)
{
    LOCK();
    s_thermal = *data;
    s_thermal.updated_at = esp_timer_get_time() / 1000;
    s_dirty.thermal = true;
    UNLOCK();
}

void telemetry_store_set_position(const position_data_t *data)
{
    LOCK();
    s_position = *data;
    s_position.updated_at = esp_timer_get_time() / 1000;
    s_dirty.position = true;
    UNLOCK();
}

void telemetry_store_set_print(const print_data_t *data)
{
    LOCK();
    // M27 reports carry no filename, overrides or fan speed, so the last known values are kept.
    char saved_filename[64];
    uint16_t saved_speed = s_print.speed_override;
    uint16_t saved_flow = s_print.flow_override;
    uint8_t saved_fan = s_print.fan_speed;
    memcpy(saved_filename, s_print.filename, sizeof(saved_filename));

    s_print = *data;
    s_print.updated_at = esp_timer_get_time() / 1000;

    if (s_print.filename[0] == '\0') {
        memcpy(s_print.filename, saved_filename, sizeof(s_print.filename));
    }
    if (s_print.speed_override == 0) s_print.speed_override = saved_speed;
    if (s_print.flow_override == 0) s_print.flow_override = saved_flow;
    if (data->fan_speed == 0 && saved_fan > 0) s_print.fan_speed = saved_fan;

    s_dirty.print = true;
    UNLOCK();
}

void telemetry_store_set_comm(const comm_stats_t *data)
{
    LOCK();
    s_comm = *data;
    s_dirty.comm = true;
    UNLOCK();
}

void telemetry_store_set_filename(const char *filename)
{
    LOCK();
    strlcpy(s_print.filename, filename ? filename : "", sizeof(s_print.filename));
    s_print.updated_at = esp_timer_get_time() / 1000;
    s_dirty.print = true;
    UNLOCK();
}

void telemetry_store_set_speed_override(uint16_t pct)
{
    LOCK();
    s_print.speed_override = pct;
    s_print.updated_at = esp_timer_get_time() / 1000;
    s_dirty.print = true;
    UNLOCK();
}

void telemetry_store_set_flow_override(uint16_t pct)
{
    LOCK();
    s_print.flow_override = pct;
    s_print.updated_at = esp_timer_get_time() / 1000;
    s_dirty.print = true;
    UNLOCK();
}

void telemetry_store_set_fan_speed(uint8_t speed)
{
    LOCK();
    s_print.fan_speed = speed;
    s_print.updated_at = esp_timer_get_time() / 1000;
    s_dirty.print = true;
    UNLOCK();
}

thermal_data_t telemetry_store_get_thermal(void)
{
    LOCK();
    thermal_data_t copy = s_thermal;
    UNLOCK();
    return copy;
}

position_data_t telemetry_store_get_position(void)
{
    LOCK();
    position_data_t copy = s_position;
    UNLOCK();
    return copy;
}

print_data_t telemetry_store_get_print(void)
{
    LOCK();
    print_data_t copy = s_print;
    UNLOCK();
    return copy;
}

comm_stats_t telemetry_store_get_comm(void)
{
    LOCK();
    comm_stats_t copy = s_comm;
    UNLOCK();
    return copy;
}

dirty_flags_t telemetry_store_get_and_clear_dirty(void)
{
    LOCK();
    dirty_flags_t copy = s_dirty;
    memset(&s_dirty, 0, sizeof(s_dirty));
    UNLOCK();
    return copy;
}

void telemetry_store_file_list_begin(void)
{
    LOCK();
    s_file_count = 0;
    s_file_list_building = 1;
    xEventGroupClearBits(s_file_list_event, FILE_LIST_READY_BIT);
    UNLOCK();
    ESP_LOGD(TAG, "File list begin");
}

void telemetry_store_file_list_add(const char *filename)
{
    LOCK();
    if (s_file_list_building && s_file_count < FILE_LIST_MAX_FILES) {
        strlcpy(s_file_list[s_file_count], filename, FILE_LIST_MAX_NAME);
        s_file_count++;
    }
    UNLOCK();
}

void telemetry_store_file_list_end(void)
{
    LOCK();
    s_file_list_building = 0;
    UNLOCK();
    xEventGroupSetBits(s_file_list_event, FILE_LIST_READY_BIT);
    ESP_LOGI(TAG, "File list complete: %d files", s_file_count);
}

int telemetry_store_get_file_list(char out[][FILE_LIST_MAX_NAME], int max_files)
{
    LOCK();
    int count = s_file_count;
    if (count > max_files) count = max_files;
    for (int i = 0; i < count; i++) {
        strlcpy(out[i], s_file_list[i], FILE_LIST_MAX_NAME);
    }
    UNLOCK();
    return count;
}

bool telemetry_store_wait_file_list(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_file_list_event, FILE_LIST_READY_BIT,
        pdTRUE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & FILE_LIST_READY_BIT) != 0;
}
