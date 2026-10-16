#include "power_manager.h"
#include "settings_store.h"
#include "telemetry_store.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "cmd_scheduler.h"
#include "cmd_dispatcher.h"
#include "marlin_comm.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_mgr";

#define CHECK_INTERVAL_MS     2000
#define TEMP_ACTIVATE_C       50.0f
#define TEMP_DEACTIVATE_C     40.0f
#define M155_INTERVAL_S       5

static volatile power_state_t s_state = POWER_STANDBY;
static TaskHandle_t s_task_hdl = NULL;
static volatile bool s_activate_requested = false;
static volatile bool s_activating = false;

static int64_t s_cool_since_ms = 0;

static int64_t s_manual_activate_ms = 0;

static uint32_t s_activation_hold_ms = 300000;
static uint32_t s_cooldown_timeout_ms = 300000;

static bool s_m155_sent = false;

static bool s_wifi_initialized = false;

static bool s_web_initialized = false;

static bool s_scheduler_initialized = false;
static bool s_scheduler_started = false;

static void send_m155(int interval_s)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "M155 S%d", interval_s);
    marlin_comm_send(cmd, 0);
}

static esp_err_t activate_wifi_subsystem(void)
{
    ESP_LOGI(TAG, "Activating WiFi subsystem...");

    if (!s_wifi_initialized) {
        esp_err_t ret = wifi_manager_init(WIFI_MGR_MODE_STA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_wifi_initialized = true;
    }

    esp_err_t ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_mgr_status_t wifi = wifi_manager_get_status();
    ESP_LOGI(TAG, "WiFi connected: %s", wifi.ip_addr);

    if (!s_scheduler_initialized) {
        cmd_dispatcher_init();
        cmd_scheduler_init();
        s_scheduler_initialized = true;
    }
    if (!s_scheduler_started) {
        cmd_scheduler_start();
        s_scheduler_started = true;
    } else {
        cmd_scheduler_pause(false);
    }

    if (!s_web_initialized) {
        web_server_init();
        s_web_initialized = true;
    }
    web_server_start();

    send_m155(1);

    ESP_LOGI(TAG, "WiFi subsystem ACTIVE");
    return ESP_OK;
}

static void deactivate_wifi_subsystem(void)
{
    ESP_LOGI(TAG, "Deactivating WiFi subsystem...");

    web_server_stop();

    cmd_scheduler_pause(true);

    wifi_manager_stop();

    send_m155(M155_INTERVAL_S);

    ESP_LOGI(TAG, "WiFi subsystem STANDBY");
}

static void wifi_activate_task(void *arg)
{
    int64_t manual_ms = (int64_t)(intptr_t)arg;

    esp_err_t ret = activate_wifi_subsystem();
    if (ret == ESP_OK) {
        s_state = POWER_ACTIVE;
        s_cool_since_ms = 0;
        if (manual_ms > 0) {
            s_manual_activate_ms = manual_ms;
        }
        ESP_LOGI(TAG, "State → ACTIVE");
    } else {
        ESP_LOGE(TAG, "WiFi activation failed, back to STANDBY");
    }

    s_activating = false;
    vTaskDelete(NULL);
}

static void power_task(void *arg)
{
    ESP_LOGI(TAG, "Power manager task started (STANDBY)");

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, NULL, pdMS_TO_TICKS(CHECK_INTERVAL_MS));

        if (!s_m155_sent && marlin_comm_get_state() >= MARLIN_COMM_IDLE) {
            send_m155(s_state == POWER_ACTIVE ? 1 : M155_INTERVAL_S);
            s_m155_sent = true;
            ESP_LOGI(TAG, "M155 enabled (marlin connected)");
        }

        thermal_data_t thermal = telemetry_store_get_thermal();
        float hotend_temp = thermal.hotend.current;
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (s_state == POWER_STANDBY && !s_activating) {
            bool temp_trigger = hotend_temp >= TEMP_ACTIVATE_C;
            bool manual_trigger = s_activate_requested;
            s_activate_requested = false;

            if (temp_trigger || manual_trigger) {
                ESP_LOGI(TAG, "Activating: %s (hotend=%.1f°C)",
                         temp_trigger ? "temp threshold" : "manual request",
                         hotend_temp);

                // wifi_manager_start blocks until connected, so it runs in its own task.
                s_activating = true;
                int64_t manual_ts = manual_trigger ? now_ms : 0;
                BaseType_t ok = xTaskCreatePinnedToCore(
                    wifi_activate_task, "wifi_act", 4096,
                    (void *)(intptr_t)manual_ts, 4, NULL, 0);
                if (ok != pdTRUE) {
                    ESP_LOGE(TAG, "Failed to create activation task");
                    s_activating = false;
                }
            }
        } else if (s_state == POWER_ACTIVE) {
            s_activate_requested = false;

            print_data_t print = telemetry_store_get_print();
            bool is_printing = (print.state == PRINT_STATE_PRINTING ||
                               print.state == PRINT_STATE_PAUSED);
            bool ws_active = web_server_has_ws_client();
            bool temp_cool = hotend_temp < TEMP_DEACTIVATE_C;

            if (is_printing || ws_active) {
                s_cool_since_ms = 0;
                continue;
            }

            if (s_manual_activate_ms > 0 &&
                (now_ms - s_manual_activate_ms) < (int64_t)s_activation_hold_ms) {
                continue;
            }

            if (temp_cool) {
                if (s_cool_since_ms == 0) {
                    s_cool_since_ms = now_ms;
                    ESP_LOGI(TAG, "Cooldown started (hotend=%.1f°C)", hotend_temp);
                } else if ((now_ms - s_cool_since_ms) >= (int64_t)s_cooldown_timeout_ms) {
                    ESP_LOGI(TAG, "Entering STANDBY (cool for %lld s)",
                             (long long)(now_ms - s_cool_since_ms) / 1000);
                    deactivate_wifi_subsystem();
                    s_state = POWER_STANDBY;
                    s_cool_since_ms = 0;
                    s_manual_activate_ms = 0;
                }
            } else {
                if (s_cool_since_ms > 0) {
                    ESP_LOGI(TAG, "Cooldown reset (hotend=%.1f°C)", hotend_temp);
                }
                s_cool_since_ms = 0;
            }
        }
    }
}

esp_err_t power_manager_init(void)
{
    s_state = POWER_STANDBY;
    s_activate_requested = false;
    s_activating = false;
    s_cool_since_ms = 0;
    s_manual_activate_ms = 0;
    s_m155_sent = false;

    settings_t cfg = settings_store_get();
    s_activation_hold_ms = (uint32_t)cfg.wifi_hold_s * 1000;
    s_cooldown_timeout_ms = (uint32_t)cfg.standby_timeout_s * 1000;

    ESP_LOGI(TAG, "Initialized (hold=%lus, standby=%lus)",
             (unsigned long)cfg.wifi_hold_s, (unsigned long)cfg.standby_timeout_s);
    return ESP_OK;
}

esp_err_t power_manager_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(power_task, "power_mgr", 4096,
                                              NULL, 4, &s_task_hdl, 0);
    if (ret != pdTRUE) return ESP_ERR_NO_MEM;

    power_manager_request_active();
    return ESP_OK;
}

void power_manager_request_active(void)
{
    s_activate_requested = true;
    ESP_LOGI(TAG, "Manual activation requested");
    if (s_task_hdl) {
        xTaskNotify(s_task_hdl, 0, eNoAction);
    }
}

power_state_t power_manager_get_state(void)
{
    return s_state;
}

bool power_manager_is_activating(void)
{
    return s_activating;
}
