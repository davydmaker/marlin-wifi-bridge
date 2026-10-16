#include "config_portal.h"
#include "config_page.h"
#include "wifi_manager.h"
#include "settings_store.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "config_portal";

static volatile bool s_saved = false;

static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static bool extract_form_value(const char *body, const char *key, char *out, size_t out_len)
{
    size_t key_len = strlen(key);
    const char *p = body;

    while ((p = strstr(p, key)) != NULL) {
        if (p != body && *(p - 1) != '&') {
            p += key_len;
            continue;
        }
        if (p[key_len] != '=') {
            p += key_len;
            continue;
        }
        p += key_len + 1;
        const char *end = strchr(p, '&');
        size_t vlen = end ? (size_t)(end - p) : strlen(p);
        if (vlen >= out_len) vlen = out_len - 1;
        memcpy(out, p, vlen);
        out[vlen] = '\0';
        url_decode(out);
        return true;
    }
    return false;
}

static void load_current_ssid(char *ssid, size_t len)
{
    ssid[0] = '\0';
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, "ssid", ssid, &len);
        nvs_close(h);
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    char ssid[33] = {0};
    load_current_ssid(ssid, sizeof(ssid));
    settings_t cfg = settings_store_get();
    char num[8];

    httpd_resp_set_type(req, "text/html");

    // A zero-length chunk terminates a chunked response, so empty values must be skipped.
    httpd_resp_send_chunk(req, CONFIG_PAGE_HEAD, -1);
    if (ssid[0]) httpd_resp_send_chunk(req, ssid, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID1, -1);
    snprintf(num, sizeof(num), "%u", cfg.wifi_hold_s);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID2, -1);
    snprintf(num, sizeof(num), "%u", cfg.standby_timeout_s);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID3, -1);
    snprintf(num, sizeof(num), "%u", cfg.dim_timeout_s);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID4, -1);
    snprintf(num, sizeof(num), "%u", cfg.poll_interval_ms);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID5, -1);
    snprintf(num, sizeof(num), "%u", cfg.cmd_timeout_ms);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_MID6, -1);
    snprintf(num, sizeof(num), "%u", cfg.cmd_max_retries);
    httpd_resp_send_chunk(req, num, -1);
    httpd_resp_send_chunk(req, CONFIG_PAGE_TAIL, -1);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

static esp_err_t save_handler(httpd_req_t *req)
{
    char body[400] = {0};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};

    if (!extract_form_value(body, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    extract_form_value(body, "pass", pass, sizeof(pass));

    ESP_LOGI(TAG, "Saving credentials: SSID=%s", ssid);
    esp_err_t ret = wifi_manager_store_credentials(ssid, pass);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_FAIL;
    }

    settings_t cfg = settings_store_get();
    char val[16];

    if (extract_form_value(body, "wifi_hold", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 60 && v <= 3600) cfg.wifi_hold_s = (uint16_t)v;
    }
    if (extract_form_value(body, "standby", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 60 && v <= 3600) cfg.standby_timeout_s = (uint16_t)v;
    }
    if (extract_form_value(body, "dim", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 10 && v <= 600) cfg.dim_timeout_s = (uint16_t)v;
    }
    if (extract_form_value(body, "poll", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 250 && v <= 5000) cfg.poll_interval_ms = (uint16_t)v;
    }
    if (extract_form_value(body, "cmd_timeout", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 1000 && v <= 10000) cfg.cmd_timeout_ms = (uint16_t)v;
    }
    if (extract_form_value(body, "cmd_retries", val, sizeof(val))) {
        int v = atoi(val);
        if (v >= 1 && v <= 10) cfg.cmd_max_retries = (uint8_t)v;
    }

    settings_store_save(&cfg);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, CONFIG_DONE_HTML);

    s_saved = true;
    return ESP_OK;
}

static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "Redirecting...");
    return ESP_OK;
}

esp_err_t config_portal_start(uint32_t timeout_s)
{
    s_saved = false;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 6;
    config.max_open_sockets = 2;
    config.uri_match_fn = httpd_uri_match_wildcard;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "Failed to start HTTP server");

    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_handler };
    httpd_register_uri_handler(server, &root);

    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_handler };
    httpd_register_uri_handler(server, &save);

    httpd_uri_t gen204 = { .uri = "/generate_204", .method = HTTP_GET, .handler = redirect_handler };
    httpd_register_uri_handler(server, &gen204);

    httpd_uri_t hotspot = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = redirect_handler };
    httpd_register_uri_handler(server, &hotspot);

    httpd_uri_t catchall = { .uri = "/*", .method = HTTP_GET, .handler = redirect_handler };
    httpd_register_uri_handler(server, &catchall);

    ESP_LOGI(TAG, "Config portal running, timeout %lu s", (unsigned long)timeout_s);

    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_s * 1000000;
    while (!s_saved && esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    httpd_stop(server);

    if (s_saved) {
        ESP_LOGI(TAG, "Settings saved, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    } else {
        ESP_LOGW(TAG, "Config portal timeout, rebooting...");
    }

    esp_restart();
}
