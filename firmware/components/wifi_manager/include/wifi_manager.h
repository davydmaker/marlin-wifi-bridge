#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_MGR_DISCONNECTED = 0,
    WIFI_MGR_CONNECTING,
    WIFI_MGR_CONNECTED,
    WIFI_MGR_FAILED,
} wifi_mgr_state_t;

typedef enum {
    WIFI_MGR_MODE_STA = 0,
    WIFI_MGR_MODE_AP,
} wifi_mgr_mode_t;

typedef struct {
    wifi_mgr_state_t state;
    int8_t           rssi;
    char             ip_addr[16];
    uint32_t         reconnect_count;
} wifi_mgr_status_t;

esp_err_t wifi_manager_init(wifi_mgr_mode_t mode);

esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
wifi_mgr_status_t wifi_manager_get_status(void);
bool wifi_manager_is_connected(void);

bool wifi_manager_has_credentials(void);
esp_err_t wifi_manager_store_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_clear_credentials(void);
