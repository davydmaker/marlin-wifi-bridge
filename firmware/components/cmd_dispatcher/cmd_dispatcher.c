#include "cmd_dispatcher.h"
#include "marlin_comm.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "cmd_disp";

static const char *DANGEROUS_CMDS[] = {
    "M112",
    "G28",
    "M524",
    "M502",
    "M500",
    "G29",
    NULL,
};

static const char *BLOCKED_CMDS[] = {
    "M997",
    NULL,
};

static bool starts_with_cmd(const char *gcode, const char *cmd)
{
    size_t len = strlen(cmd);
    if (strncmp(gcode, cmd, len) != 0) return false;
    char next = gcode[len];
    return next == '\0' || next == ' ' || next == '\n';
}

static bool is_in_list(const char *gcode, const char **list)
{
    while (*gcode == ' ') gcode++;

    for (int i = 0; list[i] != NULL; i++) {
        if (starts_with_cmd(gcode, list[i])) {
            return true;
        }
    }
    return false;
}

esp_err_t cmd_dispatcher_init(void)
{
    ESP_LOGI(TAG, "Command dispatcher initialized");
    return ESP_OK;
}

bool cmd_dispatcher_needs_confirm(const char *gcode)
{
    return is_in_list(gcode, DANGEROUS_CMDS);
}

cmd_result_t cmd_dispatcher_send(const char *gcode, bool confirmed)
{
    if (!gcode || strlen(gcode) == 0) {
        return CMD_RESULT_ERROR;
    }

    while (*gcode == ' ') gcode++;

    if (is_in_list(gcode, BLOCKED_CMDS)) {
        ESP_LOGW(TAG, "Blocked command: %s", gcode);
        return CMD_RESULT_BLOCKED;
    }

    if (is_in_list(gcode, DANGEROUS_CMDS) && !confirmed) {
        ESP_LOGW(TAG, "Dangerous command requires confirmation: %s", gcode);
        return CMD_RESULT_BLOCKED;
    }

    char first = toupper((unsigned char)gcode[0]);
    if (first != 'G' && first != 'M' && first != 'T') {
        ESP_LOGW(TAG, "Invalid G-code prefix: %s", gcode);
        return CMD_RESULT_ERROR;
    }

    esp_err_t ret = marlin_comm_send(gcode, 0);
    if (ret == ESP_ERR_TIMEOUT) {
        return CMD_RESULT_QUEUE_FULL;
    }
    if (ret != ESP_OK) {
        return CMD_RESULT_ERROR;
    }

    ESP_LOGI(TAG, "Dispatched: %s", gcode);
    return CMD_RESULT_OK;
}
