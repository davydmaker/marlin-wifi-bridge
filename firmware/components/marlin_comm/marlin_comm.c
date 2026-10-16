#include "marlin_comm.h"
#include "marlin_parser.h"
#include "telemetry_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"
#include "usb/vcp_ch34x.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "marlin_comm";

#define RX_BUF_SIZE          4096
#define RX_LINE_MAX          256
#define USB_HOST_TASK_STACK  4096
#define COMM_TASK_STACK      4096

#define EVT_OK_RECEIVED    BIT0
#define EVT_ERROR_RECEIVED BIT1

static QueueHandle_t      s_tx_queue;
static EventGroupHandle_t s_cmd_events;
static marlin_comm_state_t s_state = MARLIN_COMM_DISCONNECTED;
static cdc_acm_dev_hdl_t  s_cdc_hdl = NULL;
static TaskHandle_t       s_usb_host_task_hdl = NULL;
static TaskHandle_t       s_comm_task_hdl = NULL;

static char    s_rx_line[RX_LINE_MAX];
static size_t  s_rx_line_pos = 0;

static comm_stats_t s_stats = {};

static uint32_t  s_open_attempts = 0;

static uint32_t s_poll_interval_ms = 500;

static uint32_t s_cmd_timeout_ms = 3000;
static uint8_t  s_cmd_max_retries = 3;

static void update_comm_stats(void)
{
    s_stats.state = (s_state == MARLIN_COMM_ERROR)
        ? COMM_STATE_ERROR
        : (s_state == MARLIN_COMM_DISCONNECTED ? COMM_STATE_DISCONNECTED : COMM_STATE_CONNECTED);
    telemetry_store_set_comm(&s_stats);
}

static void process_rx_byte(uint8_t byte)
{
    s_stats.rx_bytes++;

    if (byte == '\n' || byte == '\r') {
        if (s_rx_line_pos > 0) {
            s_rx_line[s_rx_line_pos] = '\0';

            marlin_resp_type_t resp = marlin_parser_process_line(s_rx_line);

            if (resp == MARLIN_RESP_OK) {
                xEventGroupSetBits(s_cmd_events, EVT_OK_RECEIVED);
            } else if (resp == MARLIN_RESP_ERROR) {
                xEventGroupSetBits(s_cmd_events, EVT_ERROR_RECEIVED);
            }

            s_rx_line_pos = 0;
        }
        return;
    }

    if (s_rx_line_pos < RX_LINE_MAX - 1) {
        s_rx_line[s_rx_line_pos++] = (char)byte;
    }
}

static bool handle_rx(const uint8_t *data, size_t data_len, void *arg)
{
    for (size_t i = 0; i < data_len; i++) {
        process_rx_byte(data[i]);
    }
    return true;
}

static void handle_device_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
        case CDC_ACM_HOST_ERROR:
            ESP_LOGE(TAG, "USB serial error");
            s_state = MARLIN_COMM_ERROR;
            update_comm_stats();
            break;
        case CDC_ACM_HOST_SERIAL_STATE:
            ESP_LOGD(TAG, "Serial state changed");
            break;
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            ESP_LOGW(TAG, "USB device disconnected");
            s_state = MARLIN_COMM_DISCONNECTED;
            if (s_cdc_hdl) {
                cdc_acm_host_close(s_cdc_hdl);
                s_cdc_hdl = NULL;
            }
            update_comm_stats();
            break;
        default:
            break;
    }
}

static void usb_host_task(void *arg)
{
    ESP_LOGI(TAG, "USB Host task started");

    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGD(TAG, "No USB clients");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGD(TAG, "All USB devices freed");
        }
    }
}

static esp_err_t try_open_device(void)
{
    if (s_cdc_hdl) return ESP_OK;

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 5000,
        .out_buffer_size = 256,
        .in_buffer_size = RX_BUF_SIZE,
        .event_cb = handle_device_event,
        .data_cb = handle_rx,
        .user_arg = NULL,
    };

    s_open_attempts++;
    ESP_LOGI(TAG, "Scanning for CH340/CH341 USB serial device (attempt %lu)...", (unsigned long)s_open_attempts);
    esp_err_t ret = ch34x_vcp_open(CH34X_PID_AUTO, 0, &dev_config, &s_cdc_hdl);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No CH34x device found: %s (attempt %lu)", esp_err_to_name(ret), (unsigned long)s_open_attempts);
        return ESP_ERR_NOT_FOUND;
    }

    const cdc_acm_line_coding_t line_coding = {
        .dwDTERate = (uint32_t)CONFIG_MARLIN_BAUD_RATE,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    };
    ret = cdc_acm_host_line_coding_set(s_cdc_hdl, &line_coding);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Line coding set returned: %s (may be OK)", esp_err_to_name(ret));
    }

    // The printer does not answer until DTR and RTS are asserted.
    cdc_acm_host_set_control_line_state(s_cdc_hdl, true, true);

    ESP_LOGI(TAG, "CH340 USB serial device opened successfully!");
    s_state = MARLIN_COMM_CONNECTED;
    update_comm_stats();

    return ESP_OK;
}

static esp_err_t send_raw(const char *cmd)
{
    if (!s_cdc_hdl || s_state == MARLIN_COMM_DISCONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }

    char buf[MARLIN_CMD_MAX_LEN + 2];
    int len = snprintf(buf, sizeof(buf), "%s\n", cmd);

    esp_err_t ret = cdc_acm_host_data_tx_blocking(s_cdc_hdl, (const uint8_t *)buf, len, 1000);
    if (ret == ESP_OK) {
        s_stats.tx_cmds++;
        ESP_LOGD(TAG, "TX: %s", cmd);
    } else {
        ESP_LOGE(TAG, "TX failed: %s", esp_err_to_name(ret));
        s_stats.errors++;
    }
    update_comm_stats();
    return ret;
}

static void comm_task(void *arg)
{
    marlin_cmd_t cmd;

    ESP_LOGI(TAG, "Comm task started, waiting for USB device...");

    while (1) {
        while (s_state == MARLIN_COMM_DISCONNECTED || s_state == MARLIN_COMM_ERROR) {
            if (try_open_device() == ESP_OK) {
                s_state = MARLIN_COMM_IDLE;
                send_raw("");
                vTaskDelay(pdMS_TO_TICKS(500));
                xEventGroupClearBits(s_cmd_events, EVT_OK_RECEIVED | EVT_ERROR_RECEIVED);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }

        while (s_state != MARLIN_COMM_DISCONNECTED && s_state != MARLIN_COMM_ERROR) {
            if (xQueueReceive(s_tx_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
                continue;
            }

            s_state = MARLIN_COMM_WAITING_OK;
            int retries = 0;
            bool success = false;

            while (retries < s_cmd_max_retries && !success) {
                int64_t send_time = esp_timer_get_time() / 1000;

                xEventGroupClearBits(s_cmd_events, EVT_OK_RECEIVED | EVT_ERROR_RECEIVED);

                if (send_raw(cmd.cmd) != ESP_OK) {
                    s_stats.errors++;
                    break;
                }

                EventBits_t bits = xEventGroupWaitBits(s_cmd_events,
                    EVT_OK_RECEIVED | EVT_ERROR_RECEIVED,
                    pdTRUE, pdFALSE,
                    pdMS_TO_TICKS(s_cmd_timeout_ms));

                int64_t now = esp_timer_get_time() / 1000;
                s_stats.latency_ms = (int32_t)(now - send_time);

                if (bits & EVT_OK_RECEIVED) {
                    success = true;
                } else if (bits & EVT_ERROR_RECEIVED) {
                    ESP_LOGW(TAG, "Marlin returned error for: %s", cmd.cmd);
                    s_stats.errors++;
                    success = true;
                } else {
                    retries++;
                    s_stats.timeouts++;
                    ESP_LOGW(TAG, "Timeout waiting for ok (retry %d/%d): %s",
                             retries, s_cmd_max_retries, cmd.cmd);
                }

                update_comm_stats();
            }

            if (!success) {
                ESP_LOGE(TAG, "Command failed after %d retries: %s", s_cmd_max_retries, cmd.cmd);
            }

            s_state = MARLIN_COMM_IDLE;
        }
    }
}

esp_err_t marlin_comm_init(void)
{
    s_tx_queue = xQueueCreate(MARLIN_TX_QUEUE_LEN, sizeof(marlin_cmd_t));
    if (!s_tx_queue) {
        ESP_LOGE(TAG, "Failed to create TX queue");
        return ESP_ERR_NO_MEM;
    }

    s_cmd_events = xEventGroupCreate();
    if (!s_cmd_events) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    const cdc_acm_host_driver_config_t cdc_config = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 7,
        .xCoreID = 0,
        .new_dev_cb = NULL,
    };
    ESP_ERROR_CHECK(cdc_acm_host_install(&cdc_config));

    memset(&s_stats, 0, sizeof(s_stats));
    s_state = MARLIN_COMM_DISCONNECTED;

    ESP_LOGI(TAG, "Marlin comm initialized (CH340/CH341 VCP support)");
    return ESP_OK;
}

esp_err_t marlin_comm_start(void)
{
    BaseType_t ret;

    ret = xTaskCreatePinnedToCore(usb_host_task, "usb_host", USB_HOST_TASK_STACK,
                                  NULL, 7, &s_usb_host_task_hdl, 0);
    if (ret != pdTRUE) return ESP_ERR_NO_MEM;

    ret = xTaskCreatePinnedToCore(comm_task, "marlin_comm", COMM_TASK_STACK,
                                  NULL, 6, &s_comm_task_hdl, 0);
    if (ret != pdTRUE) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Marlin comm tasks started");
    return ESP_OK;
}

esp_err_t marlin_comm_send(const char *gcode, uint8_t priority)
{
    if (!gcode || strlen(gcode) == 0) return ESP_ERR_INVALID_ARG;

    marlin_cmd_t cmd = {};
    cmd.priority = priority;
    cmd.enqueued_at = esp_timer_get_time() / 1000;
    strncpy(cmd.cmd, gcode, MARLIN_CMD_MAX_LEN - 1);
    cmd.cmd[MARLIN_CMD_MAX_LEN - 1] = '\0';

    if (xQueueSend(s_tx_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "TX queue full, dropping: %s", gcode);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

marlin_comm_state_t marlin_comm_get_state(void)
{
    return s_state;
}

QueueHandle_t marlin_comm_get_tx_queue(void)
{
    return s_tx_queue;
}

void marlin_comm_set_poll_interval(uint32_t ms)
{
    if (ms < 250) ms = 250;
    if (ms > 5000) ms = 5000;
    s_poll_interval_ms = ms;
    ESP_LOGI(TAG, "Poll interval set to %lu ms", (unsigned long)ms);
}

uint32_t marlin_comm_get_poll_interval(void)
{
    return s_poll_interval_ms;
}

void marlin_comm_set_cmd_timeout(uint32_t ms)
{
    if (ms < 1000) ms = 1000;
    if (ms > 10000) ms = 10000;
    s_cmd_timeout_ms = ms;
    ESP_LOGI(TAG, "Cmd timeout set to %lu ms", (unsigned long)ms);
}

void marlin_comm_set_cmd_max_retries(uint8_t retries)
{
    if (retries < 1) retries = 1;
    if (retries > 10) retries = 10;
    s_cmd_max_retries = retries;
    ESP_LOGI(TAG, "Cmd max retries set to %u", retries);
}
