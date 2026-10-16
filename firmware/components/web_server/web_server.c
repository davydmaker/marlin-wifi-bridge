#include "web_server.h"
#include "telemetry_store.h"
#include "cmd_dispatcher.h"
#include "wifi_manager.h"
#include "marlin_comm.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "web_srv";

#define JSON_BUF_SIZE       2048
#define WS_PUSH_INTERVAL_MS 500

static httpd_handle_t s_server = NULL;

static int  s_ws_fd = -1;
static bool s_ws_active = false;

static TaskHandle_t s_ws_push_task_hdl = NULL;
static temperature_sensor_handle_t s_temp_sensor = NULL;

static float get_chip_temp(void)
{
    float temp = 0;
    if (s_temp_sensor) {
        temperature_sensor_get_celsius(s_temp_sensor, &temp);
    }
    return temp;
}

static char s_json_buf[JSON_BUF_SIZE];

static const char *build_thermal_json(void)
{
    thermal_data_t t = telemetry_store_get_thermal();
    snprintf(s_json_buf, JSON_BUF_SIZE,
        "{\"event\":\"thermal\",\"ts\":%lld,"
        "\"data\":{\"hotend\":{\"current\":%.1f,\"target\":%.1f},"
        "\"bed\":{\"current\":%.1f,\"target\":%.1f}}}",
        t.updated_at,
        t.hotend.current, t.hotend.target,
        t.bed.current, t.bed.target);
    return s_json_buf;
}

static const char *build_position_json(void)
{
    position_data_t p = telemetry_store_get_position();
    snprintf(s_json_buf, JSON_BUF_SIZE,
        "{\"event\":\"position\",\"ts\":%lld,"
        "\"data\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"e\":%.2f}}",
        p.updated_at, p.x, p.y, p.z, p.e);
    return s_json_buf;
}

static const char *build_print_json(void)
{
    print_data_t pr = telemetry_store_get_print();
    const char *state_str;
    switch (pr.state) {
        case PRINT_STATE_PRINTING: state_str = "printing"; break;
        case PRINT_STATE_PAUSED:   state_str = "paused"; break;
        case PRINT_STATE_FINISHED: state_str = "finished"; break;
        case PRINT_STATE_ERROR:    state_str = "error"; break;
        default:                   state_str = "idle"; break;
    }
    snprintf(s_json_buf, JSON_BUF_SIZE,
        "{\"event\":\"print\",\"ts\":%lld,"
        "\"data\":{\"state\":\"%s\",\"progress_pct\":%.1f,"
        "\"byte_pos\":%lu,\"byte_total\":%lu,"
        "\"filename\":\"%s\",\"speed_override\":%u,\"flow_override\":%u,\"fan_speed\":%u}}",
        pr.updated_at, state_str, pr.progress_pct,
        (unsigned long)pr.byte_pos, (unsigned long)pr.byte_total,
        pr.filename, pr.speed_override, pr.flow_override, pr.fan_speed);
    return s_json_buf;
}

static const char *build_full_telemetry_json(void)
{
    thermal_data_t t = telemetry_store_get_thermal();
    position_data_t p = telemetry_store_get_position();
    print_data_t pr = telemetry_store_get_print();
    comm_stats_t c = telemetry_store_get_comm();
    wifi_mgr_status_t w = wifi_manager_get_status();

    const char *print_state;
    switch (pr.state) {
        case PRINT_STATE_PRINTING: print_state = "printing"; break;
        case PRINT_STATE_PAUSED:   print_state = "paused"; break;
        case PRINT_STATE_FINISHED: print_state = "finished"; break;
        case PRINT_STATE_ERROR:    print_state = "error"; break;
        default:                   print_state = "idle"; break;
    }

    const char *comm_state;
    switch (c.state) {
        case COMM_STATE_CONNECTED:    comm_state = "connected"; break;
        case COMM_STATE_ERROR:        comm_state = "error"; break;
        default:                      comm_state = "disconnected"; break;
    }

    snprintf(s_json_buf, JSON_BUF_SIZE,
        "{"
        "\"ts\":%lld,"
        "\"device\":{\"heap_free\":%lu,\"uptime_s\":%llu,\"wifi_rssi\":%d,\"chip_temp\":%.1f},"
        "\"comm\":{\"state\":\"%s\",\"errors\":%lu,\"timeouts\":%lu,"
        "\"tx_cmds\":%lu,\"rx_bytes\":%lu,\"latency_ms\":%ld},"
        "\"thermal\":{\"hotend\":{\"current\":%.1f,\"target\":%.1f},"
        "\"bed\":{\"current\":%.1f,\"target\":%.1f}},"
        "\"position\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"e\":%.2f},"
        "\"print\":{\"state\":\"%s\",\"progress_pct\":%.1f,"
        "\"byte_pos\":%lu,\"byte_total\":%lu,"
        "\"filename\":\"%s\",\"speed_override\":%u,\"flow_override\":%u,\"fan_speed\":%u}"
        "}",
        (long long)(esp_timer_get_time() / 1000000),
        (unsigned long)esp_get_free_heap_size(),
        (unsigned long long)(esp_timer_get_time() / 1000000),
        (int)w.rssi,
        get_chip_temp(),
        comm_state,
        (unsigned long)c.errors, (unsigned long)c.timeouts,
        (unsigned long)c.tx_cmds, (unsigned long)c.rx_bytes,
        (long)c.latency_ms,
        t.hotend.current, t.hotend.target,
        t.bed.current, t.bed.target,
        p.x, p.y, p.z, p.e,
        print_state, pr.progress_pct,
        (unsigned long)pr.byte_pos, (unsigned long)pr.byte_total,
        pr.filename, pr.speed_override, pr.flow_override, pr.fan_speed
    );
    return s_json_buf;
}

static esp_err_t cors_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, X-Confirm");
    return ESP_OK;
}

static esp_err_t options_handler(httpd_req_t *req)
{
    cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t telemetry_handler(httpd_req_t *req)
{
    cors_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, build_full_telemetry_json());
    return ESP_OK;
}

static esp_err_t command_handler(httpd_req_t *req)
{
    cors_headers(req);

    char body[128] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *gcode_item = cJSON_GetObjectItem(root, "gcode");
    if (!gcode_item || !cJSON_IsString(gcode_item)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'gcode' field");
        return ESP_FAIL;
    }

    char confirm_hdr[8] = {0};
    bool confirmed = false;
    if (httpd_req_get_hdr_value_str(req, "X-Confirm", confirm_hdr, sizeof(confirm_hdr)) == ESP_OK) {
        confirmed = (strcmp(confirm_hdr, "true") == 0);
    }

    cmd_result_t result = cmd_dispatcher_send(gcode_item->valuestring, confirmed);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");

    switch (result) {
        case CMD_RESULT_OK:
            httpd_resp_sendstr(req, "{\"ok\":true}");
            break;
        case CMD_RESULT_BLOCKED:
            httpd_resp_set_status(req, "403 Forbidden");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Command blocked or needs X-Confirm: true\"}");
            break;
        case CMD_RESULT_QUEUE_FULL:
            httpd_resp_set_status(req, "503 Service Unavailable");
            httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Command queue full\"}");
            break;
        default:
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"ok\":false,\"error\":\"Invalid command\"}");
            break;
    }

    return ESP_OK;
}

static esp_err_t action_handler(httpd_req_t *req)
{
    cors_headers(req);

    const char *gcode = NULL;
    const char *uri = req->uri;

    if (strcmp(uri, "/api/print/pause") == 0)       gcode = "M25";
    else if (strcmp(uri, "/api/print/resume") == 0)  gcode = "M24";
    else if (strcmp(uri, "/api/print/cancel") == 0)  gcode = "M524";
    else if (strcmp(uri, "/api/emergency_stop") == 0) gcode = "M112";

    if (!gcode) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown action");
        return ESP_FAIL;
    }

    bool needs_confirm = cmd_dispatcher_needs_confirm(gcode);
    cmd_result_t result = cmd_dispatcher_send(gcode, !needs_confirm);

    httpd_resp_set_type(req, "application/json");
    if (result == CMD_RESULT_OK) {
        httpd_resp_sendstr(req, "{\"ok\":true}");
    } else {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Requires X-Confirm or blocked\"}");
    }
    return ESP_OK;
}

static esp_err_t temperature_handler(httpd_req_t *req)
{
    cors_headers(req);

    char body[128] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    char cmd[32];

    cJSON *hotend = cJSON_GetObjectItem(root, "hotend");
    if (hotend && cJSON_IsNumber(hotend)) {
        snprintf(cmd, sizeof(cmd), "M104 S%d", hotend->valueint);
        marlin_comm_send(cmd, 0);
    }

    cJSON *bed = cJSON_GetObjectItem(root, "bed");
    if (bed && cJSON_IsNumber(bed)) {
        snprintf(cmd, sizeof(cmd), "M140 S%d", bed->valueint);
        marlin_comm_send(cmd, 0);
    }

    cJSON_Delete(root);
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t polling_config_handler(httpd_req_t *req)
{
    cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    if (req->method == HTTP_GET) {
        uint32_t interval = marlin_comm_get_poll_interval();
        snprintf(s_json_buf, sizeof(s_json_buf),
            "{\"interval_ms\":%lu}", (unsigned long)interval);
        httpd_resp_sendstr(req, s_json_buf);
        return ESP_OK;
    }

    char body[64] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *interval = cJSON_GetObjectItem(root, "interval_ms");
    if (!interval || !cJSON_IsNumber(interval)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'interval_ms'");
        return ESP_FAIL;
    }

    int val = interval->valueint;
    cJSON_Delete(root);

    if (val < 250) val = 250;
    if (val > 5000) val = 5000;

    marlin_comm_set_poll_interval((uint32_t)val);

    if (val < 500) {
        snprintf(s_json_buf, sizeof(s_json_buf),
            "{\"ok\":true,\"interval_ms\":%d,\"warning\":\"Intervals below 500ms may cause instability\"}", val);
    } else {
        snprintf(s_json_buf, sizeof(s_json_buf),
            "{\"ok\":true,\"interval_ms\":%d}", val);
    }
    httpd_resp_sendstr(req, s_json_buf);
    return ESP_OK;
}

static esp_err_t files_handler(httpd_req_t *req)
{
    cors_headers(req);
    httpd_resp_set_type(req, "application/json");

    esp_err_t ret = marlin_comm_send("M20 L", 0);
    if (ret != ESP_OK) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Failed to send M20\"}");
        return ESP_OK;
    }

    if (!telemetry_store_wait_file_list(5000)) {
        httpd_resp_set_status(req, "504 Gateway Timeout");
        httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"Timeout waiting for file list\"}");
        return ESP_OK;
    }

    char files[FILE_LIST_MAX_FILES][FILE_LIST_MAX_NAME];
    int count = telemetry_store_get_file_list(files, FILE_LIST_MAX_FILES);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON *arr = cJSON_AddArrayToObject(root, "files");
    for (int i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(files[i]));
    }

    const char *json = cJSON_PrintUnformatted(root);
    if (json) {
        httpd_resp_sendstr(req, json);
        free((void *)json);
    } else {
        httpd_resp_sendstr(req, "{\"ok\":true,\"files\":[]}");
    }
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t print_start_handler(httpd_req_t *req)
{
    cors_headers(req);

    char body[128] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *fn = cJSON_GetObjectItem(root, "filename");
    if (!fn || !cJSON_IsString(fn) || strlen(fn->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'filename'");
        return ESP_FAIL;
    }

    telemetry_store_set_filename(fn->valuestring);

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "M23 %s", fn->valuestring);
    marlin_comm_send(cmd, 0);

    // Marlin needs time to open the file before M24 starts the print.
    vTaskDelay(pdMS_TO_TICKS(200));

    marlin_comm_send("M24", 0);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t file_delete_handler(httpd_req_t *req)
{
    cors_headers(req);

    char body[128] = {0};
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *fn = cJSON_GetObjectItem(root, "filename");
    if (!fn || !cJSON_IsString(fn) || strlen(fn->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'filename'");
        return ESP_FAIL;
    }

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "M30 %s", fn->valuestring);
    marlin_comm_send(cmd, 0);

    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(req);

        if (s_ws_active && s_ws_fd >= 0 && s_ws_fd != fd) {
            ESP_LOGW(TAG, "Kicking previous WS client (fd=%d) for new client (fd=%d)", s_ws_fd, fd);
            httpd_sess_trigger_close(s_server, s_ws_fd);
        }

        s_ws_fd = fd;
        s_ws_active = true;
        ESP_LOGI(TAG, "WS client connected (fd=%d)", fd);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt = {0};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len > 0 && ws_pkt.len < 128) {
        uint8_t *buf = calloc(1, ws_pkt.len + 1);
        if (buf) {
            ws_pkt.payload = buf;
            httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
            ESP_LOGD(TAG, "WS recv: %s", (char *)buf);
            free(buf);
        }
    }

    return ESP_OK;
}

static void ws_send(const char *data)
{
    if (!s_server || !s_ws_active) return;

    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = strlen(data),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fd, &ws_pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS send failed fd=%d: %s, marking inactive", s_ws_fd, esp_err_to_name(ret));
        s_ws_active = false;
        s_ws_fd = -1;
    }
}

static void ws_push_task(void *arg)
{
    ESP_LOGI(TAG, "WS push task started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WS_PUSH_INTERVAL_MS));

        if (!s_ws_active) continue;

        dirty_flags_t dirty = telemetry_store_get_and_clear_dirty();

        if (dirty.thermal)  ws_send(build_thermal_json());
        if (dirty.position) ws_send(build_position_json());
        if (dirty.print)    ws_send(build_print_json());
    }
}

bool web_server_has_ws_client(void)
{
    return s_ws_active;
}

static void register_uri(httpd_handle_t server, const char *uri,
                          httpd_method_t method, esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t h = {
        .uri = uri,
        .method = method,
        .handler = handler,
    };
    httpd_register_uri_handler(server, &h);
}

esp_err_t web_server_init(void)
{
    s_ws_fd = -1;
    s_ws_active = false;

    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&temp_cfg, &s_temp_sensor) == ESP_OK) {
        temperature_sensor_enable(s_temp_sensor);
        ESP_LOGI(TAG, "Chip temperature sensor enabled");
    } else {
        ESP_LOGW(TAG, "Failed to init chip temperature sensor");
    }

    ESP_LOGI(TAG, "Web server initialized");
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.core_id = 0;
    config.lru_purge_enable = true;
    config.max_open_sockets = 4;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    register_uri(s_server, "/api/*", HTTP_OPTIONS, options_handler);

    register_uri(s_server, "/api/telemetry", HTTP_GET, telemetry_handler);

    register_uri(s_server, "/api/config/polling", HTTP_GET, polling_config_handler);
    register_uri(s_server, "/api/config/polling", HTTP_POST, polling_config_handler);

    register_uri(s_server, "/api/command",        HTTP_POST, command_handler);
    register_uri(s_server, "/api/temperature",    HTTP_POST, temperature_handler);
    register_uri(s_server, "/api/print/pause",    HTTP_POST, action_handler);
    register_uri(s_server, "/api/print/resume",   HTTP_POST, action_handler);
    register_uri(s_server, "/api/print/cancel",   HTTP_POST, action_handler);
    register_uri(s_server, "/api/emergency_stop", HTTP_POST, action_handler);

    register_uri(s_server, "/api/files",        HTTP_GET,  files_handler);
    register_uri(s_server, "/api/print/start",  HTTP_POST, print_start_handler);
    register_uri(s_server, "/api/files/delete",  HTTP_POST, file_delete_handler);

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    xTaskCreatePinnedToCore(ws_push_task, "ws_push", 4096, NULL, 3, &s_ws_push_task_hdl, 0);

    ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (s_ws_push_task_hdl) {
        vTaskDelete(s_ws_push_task_hdl);
        s_ws_push_task_hdl = NULL;
    }

    s_ws_fd = -1;
    s_ws_active = false;

    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }

    return ESP_OK;
}
