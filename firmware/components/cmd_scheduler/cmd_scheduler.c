#include "cmd_scheduler.h"
#include "marlin_comm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "cmd_sched";

#define MAX_POLL_ENTRIES 8
#define SCHEDULER_TICK_MS 100

static poll_entry_t s_entries[MAX_POLL_ENTRIES];
static uint32_t     s_last_sent[MAX_POLL_ENTRIES];
static uint32_t     s_actual_interval[MAX_POLL_ENTRIES];
static int          s_entry_count;
static TaskHandle_t s_task_hdl;
static volatile bool s_paused = false;

static void register_entry(const char *gcode, uint32_t interval_ms,
                            uint8_t priority, bool enabled, bool only_printing)
{
    if (s_entry_count >= MAX_POLL_ENTRIES) return;

    int idx = s_entry_count;
    s_entries[idx].gcode = gcode;
    s_entries[idx].interval_ms = interval_ms;
    s_entries[idx].priority = priority;
    s_entries[idx].enabled = enabled;
    s_entries[idx].only_when_printing = only_printing;
    s_last_sent[idx] = 0;
    s_actual_interval[idx] = interval_ms;
    s_entry_count++;
}

static void scheduler_task(void *arg)
{
    ESP_LOGI(TAG, "Scheduler task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SCHEDULER_TICK_MS));

        if (s_paused) continue;

        marlin_comm_state_t comm_state = marlin_comm_get_state();
        if (comm_state == MARLIN_COMM_DISCONNECTED || comm_state == MARLIN_COMM_ERROR) {
            continue;
        }

        if (comm_state == MARLIN_COMM_WAITING_OK) {
            continue;
        }

        QueueHandle_t tx_q = marlin_comm_get_tx_queue();
        UBaseType_t q_spaces = uxQueueSpacesAvailable(tx_q);
        if (q_spaces < MARLIN_TX_QUEUE_LEN / 2) {
            continue;
        }

        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        for (int i = 0; i < s_entry_count; i++) {
            if (!s_entries[i].enabled) continue;

            uint32_t elapsed = now - s_last_sent[i];
            if (elapsed < s_actual_interval[i]) continue;

            esp_err_t ret = marlin_comm_send(s_entries[i].gcode, s_entries[i].priority);
            if (ret == ESP_OK) {
                s_last_sent[i] = now;
            }

            // One command per tick keeps the serial link from being flooded.
            break;
        }
    }
}

esp_err_t cmd_scheduler_init(void)
{
    s_entry_count = 0;

    register_entry("M105", CONFIG_POLL_TEMP_INTERVAL_MS,     1, true,  false);
    register_entry("M114", CONFIG_POLL_POSITION_INTERVAL_MS, 2, true,  false);
    register_entry("M27",  CONFIG_POLL_PROGRESS_INTERVAL_MS, 3, true,  true);
    register_entry("M220", CONFIG_POLL_PROGRESS_INTERVAL_MS, 5, true,  false);
    register_entry("M221", CONFIG_POLL_PROGRESS_INTERVAL_MS, 6, true,  false);
    ESP_LOGI(TAG, "Scheduler initialized with %d entries", s_entry_count);
    return ESP_OK;
}

esp_err_t cmd_scheduler_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(scheduler_task, "cmd_sched", 4096,
                                              NULL, 5, &s_task_hdl, 1);
    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t cmd_scheduler_set_enabled(const char *gcode, bool enabled)
{
    for (int i = 0; i < s_entry_count; i++) {
        if (strcmp(s_entries[i].gcode, gcode) == 0) {
            s_entries[i].enabled = enabled;
            ESP_LOGI(TAG, "%s %s", gcode, enabled ? "enabled" : "disabled");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t cmd_scheduler_set_interval(const char *gcode, uint32_t interval_ms)
{
    for (int i = 0; i < s_entry_count; i++) {
        if (strcmp(s_entries[i].gcode, gcode) == 0) {
            s_entries[i].interval_ms = interval_ms;
            s_actual_interval[i] = interval_ms;
            ESP_LOGI(TAG, "%s interval set to %lu ms", gcode, (unsigned long)interval_ms);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void cmd_scheduler_pause(bool paused)
{
    s_paused = paused;
    ESP_LOGI(TAG, "Scheduler %s", paused ? "paused" : "resumed");
}
