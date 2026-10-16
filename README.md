# Marlin WiFi Bridge

Telemetry and remote control bridge for Marlin based 3D printers with a USB serial port, built on an M5Stack ATOM S3 (ESP32-S3). It was built for and used with a Creality Ender 3 V3 SE. The device talks to the printer over USB serial, exposes a REST and WebSocket API over WiFi, and ships with a browser dashboard for monitoring and control.

## Status

This project is archived and kept as a reference. I no longer own the printer, so it is not maintained and nothing here will be updated against newer Marlin or ESP-IDF releases.

The original `app_main` was lost before publishing and was rebuilt from the component APIs and the documented boot flow. It compiles cleanly, but that version was never flashed to real hardware.

## Overview

```
Ender 3 V3 SE (Marlin)
   |  USB serial (CH340, 115200 8N1)
M5Stack ATOM S3 (ESP32-S3, USB host)
   |  WiFi: REST + WebSocket
Browser dashboard (TypeScript + Vite)
```

The repository has two parts:

| Path | Description |
|---|---|
| `firmware/` | ESP-IDF 5.5 project for the ATOM S3 |
| `dashboard/` | Single page web app, no runtime dependencies |

## Hardware

- M5Stack ATOM S3: ESP32-S3, 8 MB flash, no PSRAM, GC9107 128x128 LCD, button on GPIO 41.
- The ATOM S3 USB-C port runs in USB host mode and connects to the printer's USB port, which exposes a CH340 serial adapter.

### Using a different board

The printer is a USB device, so the board must act as a USB host and also have WiFi. Among ESP32 chips that means the ESP32-S3 or the ESP32-S2. The original ESP32, the C3 and the C6 have no USB host support, and the P4 has no built-in WiFi.

| Chip | USB host | WiFi | Notes |
|---|---|---|---|
| ESP32-S3 | Yes | Yes | Works as is, only board specific pins change |
| ESP32-S2 | Yes | Yes | Single core: tasks pinned to core 1 must move to core 0 |
| ESP32, C3, C6 | No | Yes | Only through the UART alternative below |
| ESP32-P4 | Yes | No | Needs an external WiFi chip |

When porting to another board:

- The board must expose the chip's native USB pins (D+ and D-) on a connector that can run in host mode. Boards whose USB port goes through a USB to serial chip cannot do this. Check how the board handles VBUS in host mode, since some devices are only detected when the host provides 5 V.
- Adjust or remove `display` (GC9107 LCD on fixed SPI pins) and `button` (GPIO 41), which are specific to the ATOM S3.
- Set the flash size in `sdkconfig.defaults` to match the board.

Alternative without USB host: wire any ESP32 with WiFi straight to a UART on the printer mainboard and replace `marlin_comm` with a plain UART driver. This requires opening the printer and matching voltage levels, and was never tried in this project.

## Firmware

### Components

| Component | Responsibility |
|---|---|
| `button` | Debounced button polling (40 ms debounce, 20 ms poll) with short and long press events |
| `display` | LCD rendering through a 128x128 RGB565 framebuffer and PWM backlight |
| `wifi_manager` | STA and AP modes, credentials stored in NVS, SNTP |
| `config_portal` | Captive portal used to enter WiFi credentials and settings |
| `settings_store` | Persistent settings in NVS |
| `marlin_comm` | USB host, CH34x driver, TX queue with timeout and retries |
| `marlin_parser` | Parses Marlin responses into the telemetry store |
| `telemetry_store` | Mutex protected telemetry state with dirty flags for WebSocket push |
| `cmd_scheduler` | Periodic polling of the printer |
| `cmd_dispatcher` | Validation layer between the API and the printer |
| `web_server` | REST API, WebSocket push and chip temperature sensor |
| `power_manager` | Standby and active state machine that turns WiFi on and off |

### Boot flow

1. Initialize NVS, display, settings, telemetry store, parser and button.
2. If there are no WiFi credentials in NVS, enter config mode:
   - Start an open access point named `MarlinWiFiBridge` at `192.168.4.1`.
   - Serve the captive portal. Saving reboots into functional mode. After 5 minutes without saving, the device reboots.
3. Otherwise, enter functional mode:
   - Start USB communication with the printer, applying the saved timeout, retry and poll settings.
   - Start the display loop and the power manager, which brings WiFi up right after boot.

### Button

| Action | Result |
|---|---|
| Short press (under 500 ms) | Wakes the display and requests WiFi activation |
| Long press (3 s) | Factory reset: clears WiFi credentials and reboots into config mode |

### Power management

In standby, WiFi is off and the printer reports temperatures on its own every 5 seconds (`M155 S5`). The device becomes active when the hotend reaches 50 C, when the button is pressed, or right after boot. When active, WiFi, the web server and the scheduler run, and auto reporting switches to every second.

The device returns to standby only when all of these hold:

- No print is running or paused.
- No WebSocket client is connected.
- The WiFi hold time after a manual activation has passed.
- The hotend has stayed below 40 C for the standby timeout.

### Settings

Settings are edited in the config portal and stored in NVS.

| Setting | Default | Range |
|---|---|---|
| WiFi hold after button press (s) | 300 | 60 to 3600 |
| Standby timeout (s) | 300 | 60 to 3600 |
| Screen dim timeout (s) | 60 | 10 to 600 |
| Poll interval (ms) | 500 | 250 to 5000 |
| Serial command timeout (ms) | 3000 | 1000 to 10000 |
| Serial command retries | 3 | 1 to 10 |

Build time options live in `firmware/main/Kconfig.projbuild`:

| Option | Default |
|---|---|
| `CONFIG_WIFI_MAX_RETRY` | 10 |
| `CONFIG_MARLIN_BAUD_RATE` | 115200 |
| `CONFIG_POLL_TEMP_INTERVAL_MS` | 2000 |
| `CONFIG_POLL_POSITION_INTERVAL_MS` | 3000 |
| `CONFIG_POLL_PROGRESS_INTERVAL_MS` | 5000 |

### Polling

When active, the scheduler sends one command per 100 ms tick. It skips a tick while a command is waiting for `ok` or while the TX queue is more than half full.

| Command | Interval | Data |
|---|---|---|
| `M105` | 2 s | Temperatures |
| `M114` | 3 s | Position |
| `M27` | 5 s | SD print progress |
| `M220` | 5 s | Speed override |
| `M221` | 5 s | Flow override |

### Command safety

Every command from the API goes through `cmd_dispatcher`:

- Commands must start with `G`, `M` or `T`.
- `M997` (firmware update) is always rejected.
- `M112`, `G28`, `M524`, `M502`, `M500` and `G29` are rejected unless the request carries the header `X-Confirm: true`.
- A full queue returns HTTP 503.

### Building

Requires ESP-IDF 5.5. Components from the ESP component registry (`usb_host_cdc_acm`, `usb_host_ch34x_vcp`, `esp_lcd_gc9107`) are downloaded on the first build.

```sh
cd firmware
idf.py build
idf.py -p PORT flash monitor
```

## API

All endpoints return JSON and send `Access-Control-Allow-Origin: *`, so the dashboard can be served from any origin.

| Method | Path | Body | Action |
|---|---|---|---|
| GET | `/api/telemetry` | | Device, comm, thermal, position and print state |
| GET | `/api/config/polling` | | Current poll interval |
| POST | `/api/config/polling` | `{"interval_ms": 1000}` | Set poll interval |
| POST | `/api/command` | `{"gcode": "M105"}` | Send a G-code command |
| POST | `/api/temperature` | `{"hotend": 200, "bed": 60}` | Set targets (`M104`, `M140`) |
| POST | `/api/print/pause` | | `M25` |
| POST | `/api/print/resume` | | `M24` |
| POST | `/api/print/cancel` | | `M524` |
| POST | `/api/emergency_stop` | | `M112` |
| GET | `/api/files` | | List SD files (`M20 L`) |
| POST | `/api/print/start` | `{"filename": "part.gcode"}` | Select and start (`M23`, `M24`) |
| POST | `/api/files/delete` | `{"filename": "part.gcode"}` | Delete from SD (`M30`) |

### WebSocket

`/ws` accepts a single client, and a new connection replaces the previous one. Every 500 ms the server pushes only the categories that changed:

```json
{"event": "thermal", "ts": 123456, "data": {"hotend": {"current": 205.3, "target": 210}, "bed": {"current": 60, "target": 60}}}
{"event": "position", "ts": 123456, "data": {"x": 120.5, "y": 80.2, "z": 15.4, "e": 1205.8}}
{"event": "print", "ts": 123456, "data": {"state": "printing", "progress_pct": 42.5, "byte_pos": 524288, "byte_total": 1232896, "filename": "part.gcode", "speed_override": 100, "flow_override": 100, "fan_speed": 255}}
```

## Dashboard

TypeScript and Vite, with Canvas for gauges and the temperature chart and plain CSS. It runs anywhere and connects to the device by IP address.

```sh
cd dashboard
npm install
npm run dev
```

| Tab | Content |
|---|---|
| Monitor | Hotend and bed gauges, print progress, position, overrides, temperature chart, serial link stats |
| Control | Temperature targets, XYZ jog with selectable step, SD file browser, pause, resume, cancel and emergency stop |
| Console | G-code input with history and a TX/RX log |
| Settings | Poll interval, chart duration, connection and device info |

Live data comes from the WebSocket, with REST polling every 5 seconds as a fallback. The WebSocket reconnects every 3 seconds after a drop. Printer controls stay disabled until the serial link reports `connected`.

## Known limitations

- The screen dim timeout setting is saved but not used: the display always dims after 60 seconds.
- The poll interval setting changes a value in `marlin_comm` that the scheduler does not read. Polling always uses the Kconfig intervals.
- Fan speed is never parsed from the printer, so it always reports 0.
- Jog sends `G91`, the move and `G90` as one multi-line command but waits for a single `ok`, so the extra `ok` replies can be matched to later commands.
- The dashboard always sends `X-Confirm: true` for console commands and print actions, which bypasses the server side confirmation. Only cancel, emergency stop, starting a print, deleting a file and jogging during a print ask for confirmation in the browser. Commands typed in the console are sent without a prompt.

## License

MIT, see [LICENSE](LICENSE).
