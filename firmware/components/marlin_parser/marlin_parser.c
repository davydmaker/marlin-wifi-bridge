#include "marlin_parser.h"
#include "telemetry_store.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "marlin_parser";

static bool s_in_file_list = false;

esp_err_t marlin_parser_init(void)
{
    ESP_LOGI(TAG, "Marlin parser initialized");
    return ESP_OK;
}

static const char *parse_temp_field(const char *p, float *current, float *target)
{
    *current = 0.0f;
    *target = 0.0f;

    if (*p == '\0') return NULL;
    p++;
    if (*p == ':') p++;

    *current = strtof(p, (char **)&p);

    while (*p == ' ') p++;
    if (*p == '/') {
        p++;
        *target = strtof(p, (char **)&p);
    }

    return p;
}

static bool parse_temperature(const char *line)
{
    const char *t_pos = strstr(line, "T:");
    if (!t_pos) return false;

    thermal_data_t data = {0};

    parse_temp_field(t_pos, &data.hotend.current, &data.hotend.target);

    const char *b_pos = strstr(line, "B:");
    if (b_pos && b_pos != line) {
        parse_temp_field(b_pos, &data.bed.current, &data.bed.target);
    }

    telemetry_store_set_thermal(&data);
    return true;
}

static bool parse_position(const char *line)
{
    const char *x_pos = strstr(line, "X:");
    if (!x_pos) return false;

    if (strstr(line, "Count") == NULL && strstr(line, "Y:") == NULL) {
        return false;
    }

    position_data_t data = {0};

    const char *p;
    p = strstr(line, "X:");
    if (p) { p += 2; data.x = strtof(p, NULL); }

    p = strstr(line, "Y:");
    if (p) { p += 2; data.y = strtof(p, NULL); }

    p = strstr(line, "Z:");
    if (p) { p += 2; data.z = strtof(p, NULL); }

    p = strstr(line, "E:");
    if (p) { p += 2; data.e = strtof(p, NULL); }

    telemetry_store_set_position(&data);
    return true;
}

static bool parse_sd_progress(const char *line)
{
    print_data_t data = telemetry_store_get_print();

    if (strstr(line, "Not SD printing")) {
        if (data.state == PRINT_STATE_PRINTING) {
            data.state = PRINT_STATE_FINISHED;
            data.progress_pct = 100.0f;
        } else {
            data.state = PRINT_STATE_IDLE;
        }
        telemetry_store_set_print(&data);
        return true;
    }

    const char *p = strstr(line, "SD printing byte");
    if (!p) return false;

    p += strlen("SD printing byte ");
    uint32_t pos = (uint32_t)strtoul(p, (char **)&p, 10);

    uint32_t total = 0;
    if (*p == '/') {
        p++;
        total = (uint32_t)strtoul(p, NULL, 10);
    }

    data.state = PRINT_STATE_PRINTING;
    data.byte_pos = pos;
    data.byte_total = total;
    data.progress_pct = (total > 0) ? (100.0f * pos / total) : 0.0f;

    telemetry_store_set_print(&data);
    return true;
}

static bool parse_filename(const char *line)
{
    const char *p = strstr(line, "Current file:");
    if (!p) return false;
    p += strlen("Current file:");
    while (*p == ' ') p++;
    if (*p == '\0' || strcmp(p, "(no file)") == 0) {
        telemetry_store_set_filename("");
        return true;
    }
    telemetry_store_set_filename(p);
    return true;
}

static bool parse_override(const char *line)
{
    const char *p;

    p = strstr(line, "FR:");
    if (p) {
        p += 3;
        int val = atoi(p);
        if (val > 0) {
            telemetry_store_set_speed_override((uint16_t)val);
            return true;
        }
    }

    p = strstr(line, "Flow:");
    if (!p) p = strstr(line, "flow:");
    if (p) {
        p += 5;
        while (*p == ' ') p++;
        int val = atoi(p);
        if (val > 0) {
            telemetry_store_set_flow_override((uint16_t)val);
            return true;
        }
    }

    return false;
}

marlin_resp_type_t marlin_parser_process_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return MARLIN_RESP_UNKNOWN;
    }

    // File names can match the patterns below, so the M20 listing is handled first.
    if (strstr(line, "Begin file list")) {
        telemetry_store_file_list_begin();
        s_in_file_list = true;
        return MARLIN_RESP_FILE_LIST;
    }

    if (strstr(line, "End file list")) {
        s_in_file_list = false;
        telemetry_store_file_list_end();
        return MARLIN_RESP_FILE_LIST;
    }

    if (s_in_file_list) {
        // "M20 L" lines are "SHORT~1.GCO long name.gcode"; plain M20 lines are "name.gco 12345".
        char fname[128];
        strlcpy(fname, line, sizeof(fname));

        char *end = fname + strlen(fname) - 1;
        while (end > fname && (*end == ' ' || *end == '\r' || *end == '\n')) {
            *end-- = '\0';
        }

        char *space = strchr(fname, ' ');
        if (space) {
            const char *after = space + 1;
            bool all_digits = true;
            for (const char *p = after; *p; p++) {
                if (*p < '0' || *p > '9') { all_digits = false; break; }
            }
            if (all_digits) {
                *space = '\0';
            } else {
                memmove(fname, after, strlen(after) + 1);
            }
        }

        if (fname[0] != '\0') {
            telemetry_store_file_list_add(fname);
        }
        return MARLIN_RESP_FILE_LIST;
    }

    if (strncmp(line, "ok", 2) == 0) {
        if (strstr(line, "T:")) {
            parse_temperature(line);
        }
        return MARLIN_RESP_OK;
    }

    if (strncmp(line, "Error:", 6) == 0 || strncmp(line, "error:", 6) == 0) {
        ESP_LOGW(TAG, "Marlin error: %s", line);
        return MARLIN_RESP_ERROR;
    }

    if (strstr(line, "echo:busy")) {
        ESP_LOGD(TAG, "Marlin busy");
        return MARLIN_RESP_BUSY;
    }

    if (strncmp(line, "echo:", 5) == 0) {
        ESP_LOGD(TAG, "Echo: %s", line + 5);
        return MARLIN_RESP_ECHO;
    }

    if (parse_temperature(line)) {
        return MARLIN_RESP_TEMP;
    }

    if (parse_position(line)) {
        return MARLIN_RESP_POSITION;
    }

    if (parse_sd_progress(line)) {
        return MARLIN_RESP_SD_PROGRESS;
    }

    if (parse_filename(line)) {
        return MARLIN_RESP_SD_PROGRESS;
    }

    if (parse_override(line)) {
        return MARLIN_RESP_ECHO;
    }

    ESP_LOGD(TAG, "Unrecognized: %.60s", line);
    return MARLIN_RESP_UNKNOWN;
}

bool marlin_parser_is_terminal(marlin_resp_type_t type)
{
    return type == MARLIN_RESP_OK || type == MARLIN_RESP_ERROR;
}
