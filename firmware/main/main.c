#include "button.h"
#include "config_portal.h"
#include "display.h"
#include "marlin_comm.h"
#include "marlin_parser.h"
#include "power_manager.h"
#include "settings_store.h"
#include "telemetry_store.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

static const char *TAG = "main";

#define CONFIG_PORTAL_TIMEOUT_S 300

static bool s_config_mode = false;

static void on_button(button_event_t event)
{
    switch (event) {
        case BUTTON_EVT_SHORT_PRESS:
            display_wake();
            if (!s_config_mode) {
                power_manager_request_active();
            }
            break;
        case BUTTON_EVT_LONG_PRESS:
            ESP_LOGW(TAG, "Factory reset: clearing WiFi credentials");
            wifi_manager_clear_credentials();
            esp_restart();
            break;
        default:
            break;
    }
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void run_config_mode(void)
{
    ESP_LOGI(TAG, "No WiFi credentials, entering config mode");
    s_config_mode = true;

    display_set_page(DISPLAY_PAGE_CONFIG);
    display_set_backlight_override(true);
    ESP_ERROR_CHECK(display_start());

    ESP_ERROR_CHECK(wifi_manager_init(WIFI_MGR_MODE_AP));
    ESP_ERROR_CHECK(wifi_manager_start());
    config_portal_start(CONFIG_PORTAL_TIMEOUT_S);
}

static void run_functional_mode(void)
{
    ESP_LOGI(TAG, "Entering functional mode");

    settings_t cfg = settings_store_get();
    ESP_ERROR_CHECK(marlin_comm_init());
    marlin_comm_set_poll_interval(cfg.poll_interval_ms);
    marlin_comm_set_cmd_timeout(cfg.cmd_timeout_ms);
    marlin_comm_set_cmd_max_retries(cfg.cmd_max_retries);
    ESP_ERROR_CHECK(marlin_comm_start());

    ESP_ERROR_CHECK(display_start());

    ESP_ERROR_CHECK(power_manager_init());
    ESP_ERROR_CHECK(power_manager_start());
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(settings_store_init());
    ESP_ERROR_CHECK(telemetry_store_init());
    ESP_ERROR_CHECK(marlin_parser_init());
    ESP_ERROR_CHECK(button_init(on_button));
    ESP_ERROR_CHECK(button_start());

    if (wifi_manager_has_credentials()) {
        run_functional_mode();
    } else {
        run_config_mode();
    }
}
