#pragma once

static const char CONFIG_PAGE_HEAD[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Marlin WiFi Bridge</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;margin:0;padding:20px;display:flex;justify-content:center;}"
    ".c{max-width:400px;width:100%;}"
    "h1{color:#06b6d4;text-align:center;margin-bottom:8px;}"
    "p.s{text-align:center;color:#888;margin-top:0;}"
    "label{display:block;margin-top:12px;font-size:14px;color:#aaa;}"
    "input[type=text],input[type=password],input[type=number]"
    "{width:100%;padding:10px;margin-top:4px;border:1px solid #333;border-radius:8px;"
    "background:#16213e;color:#fff;font-size:16px;box-sizing:border-box;}"
    "input:focus{outline:none;border-color:#06b6d4;}"
    "button{width:100%;padding:14px;margin-top:24px;border:none;border-radius:8px;"
    "background:#06b6d4;color:#000;font-size:16px;font-weight:bold;cursor:pointer;}"
    "button:active{background:#0891b2;}"
    "details{margin-top:20px;border:1px solid #333;border-radius:8px;padding:12px;}"
    "summary{cursor:pointer;color:#06b6d4;font-weight:bold;}"
    ".row{display:flex;gap:10px;}"
    ".row>div{flex:1;}"
    ".hint{font-size:11px;color:#666;margin-top:2px;}"
    "</style></head>"
    "<body><div class='c'>"
    "<h1>Marlin WiFi Bridge</h1>"
    "<p class='s'>Configure your printer bridge</p>"
    "<form method='POST' action='/save'>"
    "<label>WiFi Network (SSID)</label>"
    "<input type='text' name='ssid' maxlength='32' value='";

static const char CONFIG_PAGE_MID1[] =
    "' required autofocus>"
    "<label>Password</label>"
    "<input type='password' name='pass' maxlength='64'>"
    "<details><summary>Advanced Settings</summary>"
    "<div class='row'><div>"
    "<label>WiFi Hold (s)</label>"
    "<input type='number' name='wifi_hold' min='60' max='3600' value='";

static const char CONFIG_PAGE_MID2[] =
    "'><p class='hint'>Keep WiFi on after button press</p>"
    "</div><div>"
    "<label>Standby (s)</label>"
    "<input type='number' name='standby' min='60' max='3600' value='";

static const char CONFIG_PAGE_MID3[] =
    "'><p class='hint'>Cool-down before sleep</p>"
    "</div></div>"
    "<div class='row'><div>"
    "<label>Screen Dim (s)</label>"
    "<input type='number' name='dim' min='10' max='600' value='";

static const char CONFIG_PAGE_MID4[] =
    "'><p class='hint'>Auto-dim timeout</p>"
    "</div><div>"
    "<label>Poll (ms)</label>"
    "<input type='number' name='poll' min='250' max='5000' value='";

static const char CONFIG_PAGE_MID5[] =
    "'><p class='hint'>Printer poll interval</p>"
    "</div></div>"
    "<div class='row'><div>"
    "<label>Timeout (ms)</label>"
    "<input type='number' name='cmd_timeout' min='1000' max='10000' value='";

static const char CONFIG_PAGE_MID6[] =
    "'><p class='hint'>Serial cmd timeout</p>"
    "</div><div>"
    "<label>Retries</label>"
    "<input type='number' name='cmd_retries' min='1' max='10' value='";

static const char CONFIG_PAGE_TAIL[] =
    "'><p class='hint'>Max retry attempts</p>"
    "</div></div>"
    "</details>"
    "<button type='submit'>Save & Reboot</button>"
    "</form>"
    "</div></body></html>";

static const char CONFIG_DONE_HTML[] =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Saved</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#e0e0e0;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}"
    ".ok{text-align:center;}"
    "h1{color:#22c55e;}"
    "p{color:#888;}"
    "</style></head>"
    "<body><div class='ok'>"
    "<h1>Saved!</h1>"
    "<p>Rebooting in 3 seconds...</p>"
    "</div></body></html>";
