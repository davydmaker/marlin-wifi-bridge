#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_GPIO         41
#define POLL_INTERVAL_MS    20
#define DEBOUNCE_MS         40
#define SHORT_PRESS_MAX_MS  500
#define LONG_PRESS_MS       3000
#define VERY_LONG_PRESS_MS  8000

static button_callback_t s_callback = NULL;
static TaskHandle_t s_task_hdl = NULL;

static void button_task(void *arg)
{
    bool pressed = false;
    int64_t press_start = 0;
    bool long_fired = false;
    bool very_long_fired = false;
    int debounce_count = 0;
    const int debounce_ticks = DEBOUNCE_MS / POLL_INTERVAL_MS;

    while (1) {
        bool level = gpio_get_level(BUTTON_GPIO) == 0;

        if (level && !pressed) {
            debounce_count++;
            if (debounce_count >= debounce_ticks) {
                pressed = true;
                press_start = esp_timer_get_time() / 1000;
                long_fired = false;
                very_long_fired = false;
            }
        } else if (!level && pressed) {
            debounce_count = 0;
            int64_t duration = (esp_timer_get_time() / 1000) - press_start;
            pressed = false;

            if (!long_fired && duration < SHORT_PRESS_MAX_MS) {
                ESP_LOGI(TAG, "Short press (%lld ms)", (long long)duration);
                if (s_callback) s_callback(BUTTON_EVT_SHORT_PRESS);
            }
        } else if (!level) {
            debounce_count = 0;
        }

        if (pressed) {
            int64_t duration = (esp_timer_get_time() / 1000) - press_start;

            if (!very_long_fired && duration >= VERY_LONG_PRESS_MS) {
                very_long_fired = true;
                ESP_LOGI(TAG, "Very long press (%lld ms)", (long long)duration);
                if (s_callback) s_callback(BUTTON_EVT_VERY_LONG_PRESS);
            } else if (!long_fired && duration >= LONG_PRESS_MS) {
                long_fired = true;
                ESP_LOGI(TAG, "Long press (%lld ms)", (long long)duration);
                if (s_callback) s_callback(BUTTON_EVT_LONG_PRESS);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t button_init(button_callback_t cb)
{
    s_callback = cb;

    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_cfg);

    ESP_LOGI(TAG, "Button initialized on GPIO%d", BUTTON_GPIO);
    return ESP_OK;
}

esp_err_t button_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(button_task, "button", 2048,
                                              NULL, 3, &s_task_hdl, 0);
    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}
