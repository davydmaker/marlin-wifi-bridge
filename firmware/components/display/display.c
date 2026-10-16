#include "display.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9107.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "display";

#define LCD_SPI_HOST    SPI2_HOST
#define LCD_PIN_MOSI    21
#define LCD_PIN_CLK     17
#define LCD_PIN_CS      15
#define LCD_PIN_DC      33
#define LCD_PIN_RST     34
#define LCD_PIN_BL      16
#define LCD_H_RES       128
#define LCD_V_RES       128
#define LCD_BIT_DEPTH   16

#define DISPLAY_REFRESH_MS  500
#define FONT_W  6
#define FONT_H  8

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_FREQ_HZ      5000
#define BL_RESOLUTION   LEDC_TIMER_8_BIT

#define BRIGHTNESS_OFF     0
#define BRIGHTNESS_IDLE   20
#define BRIGHTNESS_ACTIVE 40

#define ACTIVE_TIMEOUT_MS 60000
#define WS_OFF_HYSTERESIS_MS 5000

static esp_lcd_panel_handle_t s_panel = NULL;
static display_page_t s_current_page = DISPLAY_PAGE_STATUS;
static TaskHandle_t s_task_hdl = NULL;

static uint8_t s_brightness = BRIGHTNESS_IDLE;
static bool s_backlight_override = false;
static int64_t s_last_interaction = 0;
static int64_t s_wake_until = 0;

static uint16_t s_fb[LCD_H_RES * LCD_V_RES];

#include "font6x8.inc"

static void bl_init_pwm(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = BL_RESOLUTION,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .gpio_num = LCD_PIN_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);
}

static void bl_set_duty(uint8_t pct)
{
    if (pct > 100) pct = 100;
    uint32_t duty = (uint32_t)(255 * pct / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

static void fb_clear(uint16_t color)
{
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        s_fb[i] = color;
    }
}

static void fb_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && x < LCD_H_RES && y >= 0 && y < LCD_V_RES) {
        s_fb[y * LCD_H_RES + x] = color;
    }
}

static void fb_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < 32 || c > 127) c = '?';
    const uint8_t *glyph = font6x8 + (c - 32) * FONT_H;

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            fb_pixel(x + col, y + row, (bits & (0x80 >> col)) ? fg : bg);
        }
    }
}

static void fb_string(int x, int y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        fb_char(x, y, *str, fg, bg);
        x += FONT_W;
        str++;
    }
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return __builtin_bswap16(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void fb_hline(int x, int y, int w, uint16_t color)
{
    for (int i = 0; i < w && (x + i) < LCD_H_RES; i++) {
        fb_pixel(x + i, y, color);
    }
}

static void flush_fb(void)
{
    if (s_panel) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
    }
}

static void render_status_page(void)
{
    uint16_t bg = rgb565(10, 15, 30);
    uint16_t cyan = rgb565(0, 200, 255);
    uint16_t green = rgb565(0, 200, 0);
    uint16_t yellow = rgb565(255, 200, 0);
    uint16_t red = rgb565(255, 50, 50);
    uint16_t dim = rgb565(120, 120, 120);

    fb_clear(bg);

    fb_string(1, 1, "MARLIN WIFI BRIDGE", cyan, bg);
    fb_hline(0, 10, 128, cyan);

    wifi_mgr_status_t w = wifi_manager_get_status();
    char line[24];

    fb_string(1, 20, "WiFi:", dim, bg);
    switch (w.state) {
        case WIFI_MGR_CONNECTED:
            fb_string(36, 20, "CONNECTED", green, bg);
            break;
        case WIFI_MGR_CONNECTING:
            fb_string(36, 20, "CONNECTING...", yellow, bg);
            break;
        case WIFI_MGR_FAILED:
            fb_string(36, 20, "FAILED", red, bg);
            break;
        default:
            fb_string(36, 20, "OFF", dim, bg);
            break;
    }

    if (w.state == WIFI_MGR_CONNECTED && w.ip_addr[0] != '\0') {
        fb_string(1, 38, "Dashboard:", dim, bg);
        snprintf(line, sizeof(line), "%s", w.ip_addr);
        fb_string(1, 50, line, green, bg);
    }

    fb_string(1, 110, "FW:" __DATE__, dim, bg);
}

static void render_config_page(void)
{
    uint16_t bg = rgb565(40, 0, 60);
    uint16_t fg = rgb565(255, 255, 255);
    uint16_t yellow = rgb565(255, 200, 0);

    fb_clear(bg);

    fb_string(1, 1, "CONFIG MODE", yellow, bg);
    fb_hline(0, 10, 128, yellow);

    fb_string(1, 20, "Connect to WiFi:", fg, bg);
    fb_string(1, 34, "MarlinWiFiBridge", yellow, bg);

    fb_string(1, 52, "Then open:", fg, bg);
    fb_string(1, 66, "192.168.4.1", yellow, bg);

    fb_string(1, 90, "Long press to", fg, bg);
    fb_string(1, 100, "exit config mode", fg, bg);
}

static void display_task(void *arg)
{
    ESP_LOGI(TAG, "Display task started");
    int64_t ws_disconnected_at = 0;
    bool was_ws_active = false;

    while (1) {
        bool ws_active = web_server_has_ws_client();
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (was_ws_active && !ws_active) {
            ws_disconnected_at = now_ms;
        }
        was_ws_active = ws_active;

        bool wake_override = (s_wake_until > 0 && now_ms < s_wake_until);
        uint8_t target_brightness;
        if (s_backlight_override) {
            target_brightness = BRIGHTNESS_ACTIVE;
        } else if (wake_override) {
            int64_t idle_ms = now_ms - s_last_interaction;
            target_brightness = (idle_ms < ACTIVE_TIMEOUT_MS) ? BRIGHTNESS_ACTIVE : BRIGHTNESS_IDLE;
        } else if (ws_active) {
            target_brightness = BRIGHTNESS_OFF;
        } else if (ws_disconnected_at > 0 && (now_ms - ws_disconnected_at) < WS_OFF_HYSTERESIS_MS) {
            target_brightness = BRIGHTNESS_OFF;
        } else {
            int64_t idle_ms = now_ms - s_last_interaction;
            target_brightness = (idle_ms < ACTIVE_TIMEOUT_MS) ? BRIGHTNESS_ACTIVE : BRIGHTNESS_IDLE;
        }

        if (target_brightness != s_brightness) {
            s_brightness = target_brightness;
            bl_set_duty(s_brightness);
        }

        if (s_brightness > BRIGHTNESS_OFF) {
            switch (s_current_page) {
                case DISPLAY_PAGE_STATUS:  render_status_page();  break;
                case DISPLAY_PAGE_CONFIG:  render_config_page();  break;
                default: render_status_page(); break;
            }
            flush_fb();
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

esp_err_t display_init(void)
{
    bl_init_pwm();
    bl_set_duty(BRIGHTNESS_ACTIVE);

    const spi_bus_config_t bus_cfg = GC9107_PANEL_BUS_SPI_CONFIG(LCD_PIN_CLK, LCD_PIN_MOSI,
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_cfg = GC9107_PANEL_IO_SPI_CONFIG(LCD_PIN_CS, LCD_PIN_DC, NULL, NULL);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = LCD_BIT_DEPTH,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9107(io_handle, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 2, 1));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    s_last_interaction = esp_timer_get_time() / 1000;
    s_brightness = BRIGHTNESS_ACTIVE;

    uint16_t bg = rgb565(10, 15, 30);
    uint16_t cyan = rgb565(0, 200, 255);
    fb_clear(bg);
    fb_string(1, 1, "MARLIN WIFI BRIDGE", cyan, bg);
    fb_string(1, 14, "Booting...", rgb565(120, 120, 120), bg);
    flush_fb();

    ESP_LOGI(TAG, "Display initialized (GC9107 128x128, PWM backlight)");
    return ESP_OK;
}

esp_err_t display_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(display_task, "display", 4096,
                                              NULL, 2, &s_task_hdl, 1);
    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}

void display_next_page(void)
{
    s_last_interaction = esp_timer_get_time() / 1000;
    s_current_page = DISPLAY_PAGE_STATUS;
}

void display_set_page(display_page_t page)
{
    s_current_page = page;
    s_last_interaction = esp_timer_get_time() / 1000;
}

void display_set_brightness(uint8_t pct)
{
    s_brightness = pct;
    bl_set_duty(pct);
}

void display_set_backlight_override(bool always_on)
{
    s_backlight_override = always_on;
}

void display_wake(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    s_last_interaction = now;
    s_wake_until = now + ACTIVE_TIMEOUT_MS;
}
