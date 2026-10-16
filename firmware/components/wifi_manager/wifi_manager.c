#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_mgr";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define NVS_NAMESPACE      "wifi_cfg"
#define NVS_KEY_SSID       "ssid"
#define NVS_KEY_PASS       "pass"

#define AP_SSID            "MarlinWiFiBridge"
#define AP_IP              "192.168.4.1"

static EventGroupHandle_t s_wifi_event_group;
static wifi_mgr_status_t  s_status;
static int                s_retry_count;
static wifi_mgr_mode_t   s_mode;
static volatile bool      s_stopping = false;
static bool               s_sntp_initialized = false;

bool wifi_manager_has_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t len = 0;
    esp_err_t ret = nvs_get_str(h, NVS_KEY_SSID, NULL, &len);
    nvs_close(h);

    return (ret == ESP_OK && len > 1);
}

esp_err_t wifi_manager_store_credentials(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "NVS open failed");

    esp_err_t ret = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (ret == ESP_OK) {
        ret = nvs_set_str(h, NVS_KEY_PASS, password ? password : "");
    }
    if (ret == ESP_OK) {
        ret = nvs_commit(h);
    }
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Credentials stored for SSID: %s", ssid);
    }
    return ret;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h), TAG, "NVS open failed");
    nvs_erase_all(h);
    esp_err_t ret = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Credentials cleared");
    return ret;
}

static bool load_credentials_from_nvs(wifi_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;

    size_t ssid_len = sizeof(cfg->sta.ssid);
    size_t pass_len = sizeof(cfg->sta.password);

    esp_err_t r1 = nvs_get_str(h, NVS_KEY_SSID, (char *)cfg->sta.ssid, &ssid_len);
    esp_err_t r2 = nvs_get_str(h, NVS_KEY_PASS, (char *)cfg->sta.password, &pass_len);
    nvs_close(h);

    if (r1 == ESP_OK && ssid_len > 1) {
        if (r2 != ESP_OK) cfg->sta.password[0] = '\0';
        ESP_LOGI(TAG, "Loaded credentials from NVS, SSID: %s", cfg->sta.ssid);
        return true;
    }
    return false;
}

static void sta_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        s_status.state = WIFI_MGR_CONNECTING;
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_status.state = WIFI_MGR_DISCONNECTED;
        s_status.rssi = 0;
        memset(s_status.ip_addr, 0, sizeof(s_status.ip_addr));

        if (s_stopping) {
            ESP_LOGI(TAG, "WiFi stopped intentionally, not reconnecting");
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            return;
        }

        if (s_retry_count < CONFIG_WIFI_MAX_RETRY) {
            s_retry_count++;
            s_status.reconnect_count++;
            s_status.state = WIFI_MGR_CONNECTING;
            ESP_LOGW(TAG, "Reconnecting... attempt %d/%d", s_retry_count, CONFIG_WIFI_MAX_RETRY);
            vTaskDelay(pdMS_TO_TICKS(1000 * s_retry_count));
            esp_wifi_connect();
        } else {
            s_status.state = WIFI_MGR_FAILED;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Max retries reached, will reboot in 10s");
            vTaskDelay(pdMS_TO_TICKS(10000));
            esp_restart();
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_status.ip_addr, sizeof(s_status.ip_addr),
                 IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connected, IP: %s", s_status.ip_addr);

        s_status.state = WIFI_MGR_CONNECTED;
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_status.rssi = ap_info.rssi;
        }
    }
}

static void sntp_init_time(void)
{
    if (s_sntp_initialized) return;
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(2,
        ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com"));
    esp_netif_sntp_init(&config);
    s_sntp_initialized = true;
}

static esp_err_t init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    if (!load_credentials_from_nvs(&wifi_config)) {
        ESP_LOGE(TAG, "No credentials in NVS, STA init should not run without them");
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    return ESP_OK;
}

static esp_err_t init_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = sizeof(AP_SSID) - 1,
            .channel = 1,
            .max_connection = 2,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    s_status.state = WIFI_MGR_CONNECTED;
    strlcpy(s_status.ip_addr, AP_IP, sizeof(s_status.ip_addr));

    ESP_LOGI(TAG, "AP mode: SSID=%s, IP=%s", AP_SSID, AP_IP);
    return ESP_OK;
}

esp_err_t wifi_manager_init(wifi_mgr_mode_t mode)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = WIFI_MGR_DISCONNECTED;
    s_retry_count = 0;
    s_mode = mode;
    s_stopping = false;

    if (mode == WIFI_MGR_MODE_AP) {
        return init_ap();
    }
    return init_sta();
}

esp_err_t wifi_manager_start(void)
{
    s_stopping = false;
    s_retry_count = 0;

    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_mode == WIFI_MGR_MODE_AP) {
        return ESP_OK;
    }

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));

    if (bits & WIFI_CONNECTED_BIT) {
        sntp_init_time();
        return ESP_OK;
    }

    ESP_LOGE(TAG, "WiFi connection timeout");
    return ESP_FAIL;
}

esp_err_t wifi_manager_stop(void)
{
    s_stopping = true;
    esp_err_t ret = esp_wifi_stop();
    s_status.state = WIFI_MGR_DISCONNECTED;
    s_status.rssi = 0;
    memset(s_status.ip_addr, 0, sizeof(s_status.ip_addr));
    ESP_LOGI(TAG, "WiFi stopped");
    return ret;
}

wifi_mgr_status_t wifi_manager_get_status(void)
{
    if (s_status.state == WIFI_MGR_CONNECTED && s_mode == WIFI_MGR_MODE_STA) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_status.rssi = ap_info.rssi;
        }
    }
    return s_status;
}

bool wifi_manager_is_connected(void)
{
    return s_status.state == WIFI_MGR_CONNECTED;
}
