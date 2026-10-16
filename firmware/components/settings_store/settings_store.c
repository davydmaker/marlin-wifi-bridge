#include "settings_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE   "settings"
#define KEY_WIFI_HOLD   "wifi_hold_s"
#define KEY_STANDBY     "standby_s"
#define KEY_DIM         "dim_s"
#define KEY_POLL        "poll_ms"
#define KEY_CMD_TIMEOUT "cmd_tout_ms"
#define KEY_CMD_RETRIES "cmd_retries"

#define DEFAULT_WIFI_HOLD_S       300
#define DEFAULT_STANDBY_TIMEOUT_S 300
#define DEFAULT_DIM_TIMEOUT_S     60
#define DEFAULT_POLL_INTERVAL_MS  500
#define DEFAULT_CMD_TIMEOUT_MS    3000
#define DEFAULT_CMD_MAX_RETRIES   3

static settings_t s_settings;

esp_err_t settings_store_init(void)
{
    s_settings.wifi_hold_s       = DEFAULT_WIFI_HOLD_S;
    s_settings.standby_timeout_s = DEFAULT_STANDBY_TIMEOUT_S;
    s_settings.dim_timeout_s     = DEFAULT_DIM_TIMEOUT_S;
    s_settings.poll_interval_ms  = DEFAULT_POLL_INTERVAL_MS;
    s_settings.cmd_timeout_ms    = DEFAULT_CMD_TIMEOUT_MS;
    s_settings.cmd_max_retries   = DEFAULT_CMD_MAX_RETRIES;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No saved settings, using defaults");
        return ESP_OK;
    }

    nvs_get_u16(h, KEY_WIFI_HOLD, &s_settings.wifi_hold_s);
    nvs_get_u16(h, KEY_STANDBY,   &s_settings.standby_timeout_s);
    nvs_get_u16(h, KEY_DIM,       &s_settings.dim_timeout_s);
    nvs_get_u16(h, KEY_POLL,      &s_settings.poll_interval_ms);
    nvs_get_u16(h, KEY_CMD_TIMEOUT, &s_settings.cmd_timeout_ms);
    uint8_t retries = s_settings.cmd_max_retries;
    if (nvs_get_u8(h, KEY_CMD_RETRIES, &retries) == ESP_OK) {
        s_settings.cmd_max_retries = retries;
    }
    nvs_close(h);

    ESP_LOGI(TAG, "Loaded: wifi_hold=%us standby=%us dim=%us poll=%ums timeout=%ums retries=%u",
             s_settings.wifi_hold_s, s_settings.standby_timeout_s,
             s_settings.dim_timeout_s, s_settings.poll_interval_ms,
             s_settings.cmd_timeout_ms, s_settings.cmd_max_retries);

    return ESP_OK;
}

settings_t settings_store_get(void)
{
    return s_settings;
}

esp_err_t settings_store_save(const settings_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_u16(h, KEY_WIFI_HOLD, s->wifi_hold_s);
    nvs_set_u16(h, KEY_STANDBY,   s->standby_timeout_s);
    nvs_set_u16(h, KEY_DIM,       s->dim_timeout_s);
    nvs_set_u16(h, KEY_POLL,      s->poll_interval_ms);
    nvs_set_u16(h, KEY_CMD_TIMEOUT, s->cmd_timeout_ms);
    nvs_set_u8(h, KEY_CMD_RETRIES, s->cmd_max_retries);

    ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        s_settings = *s;
        ESP_LOGI(TAG, "Saved: wifi_hold=%us standby=%us dim=%us poll=%ums timeout=%ums retries=%u",
                 s->wifi_hold_s, s->standby_timeout_s,
                 s->dim_timeout_s, s->poll_interval_ms,
                 s->cmd_timeout_ms, s->cmd_max_retries);
    }

    return ret;
}
