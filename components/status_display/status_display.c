#include "status_display.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "irrigation_model.h"
#include "moisture_manager.h"
#include "pump_controller.h"
#include "scheduler.h"

#define OLED_I2C_PORT I2C_NUM_0
#define OLED_SDA_GPIO 12
#define OLED_SCL_GPIO 13
#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_REFRESH_MS 2000

static const char *TAG = "status_display";
static uint8_t g_fb[OLED_WIDTH * OLED_PAGES];

static const uint8_t FONT_DIGITS[10][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e},
};

static const uint8_t FONT_LETTERS[26][5] = {
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

static esp_err_t oled_write(uint8_t control, const uint8_t *data, size_t len)
{
    uint8_t out[17];
    if (len > sizeof(out) - 1) return ESP_ERR_INVALID_SIZE;
    out[0] = control;
    memcpy(&out[1], data, len);
    return i2c_master_write_to_device(OLED_I2C_PORT, OLED_ADDR, out, len + 1, pdMS_TO_TICKS(100));
}

static esp_err_t oled_cmd(uint8_t cmd)
{
    return oled_write(0x00, &cmd, 1);
}

static esp_err_t oled_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    esp_err_t err = i2c_param_config(OLED_I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(OLED_I2C_PORT, conf.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    const uint8_t init[] = {0xae, 0x20, 0x00, 0xb0, 0xc8, 0x00, 0x10, 0x40, 0x81, 0x7f,
                            0xa1, 0xa6, 0xa8, 0x3f, 0xa4, 0xd3, 0x00, 0xd5, 0x80, 0xd9,
                            0xf1, 0xda, 0x12, 0xdb, 0x40, 0x8d, 0x14, 0xaf};
    for (size_t i = 0; i < sizeof(init); i++) {
        esp_err_t err = oled_cmd(init[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

static const uint8_t *glyph_for(char ch)
{
    static const uint8_t blank[5] = {0};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t percent[5] = {0x23, 0x13, 0x08, 0x64, 0x62};
    if (ch >= '0' && ch <= '9') return FONT_DIGITS[ch - '0'];
    ch = (char)toupper((unsigned char)ch);
    if (ch >= 'A' && ch <= 'Z') return FONT_LETTERS[ch - 'A'];
    if (ch == ':') return colon;
    if (ch == '-') return dash;
    if (ch == '%') return percent;
    return blank;
}

static void fb_clear(void)
{
    memset(g_fb, 0, sizeof(g_fb));
}

static void draw_text(int x, int page, const char *text)
{
    if (page < 0 || page >= OLED_PAGES) return;
    while (*text && x < OLED_WIDTH - 5) {
        const uint8_t *glyph = glyph_for(*text++);
        for (int i = 0; i < 5 && x + i < OLED_WIDTH; i++) {
            if (x + i >= 0) g_fb[page * OLED_WIDTH + x + i] = glyph[i];
        }
        x += 6;
    }
}

static void oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        oled_cmd((uint8_t)(0xb0 | page));
        oled_cmd(0x00);
        oled_cmd(0x10);
        for (int x = 0; x < OLED_WIDTH; x += 16) {
            oled_write(0x40, &g_fb[page * OLED_WIDTH + x], 16);
        }
    }
}

static const char *short_pump_state(pump_state_t state)
{
    switch (state) {
    case PUMP_STATE_IDLE: return "IDLE";
    case PUMP_STATE_MANUAL_RUNNING: return "MANUAL RUN";
    case PUMP_STATE_SCHEDULED_RUNNING: return "SCHED RUN";
    case PUMP_STATE_COOLDOWN: return "COOLDOWN";
    case PUMP_STATE_LOCKED_OUT_LOW_RESERVOIR: return "RES LOW";
    case PUMP_STATE_LOCKED_OUT_MAX_RUNTIME: return "MAX RUNTIME";
    case PUMP_STATE_FAULT: return "FAULT";
    default: return "UNKNOWN";
    }
}

static void status_display_task(void *arg)
{
    (void)arg;
    esp_err_t err = oled_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED not available on SDA GPIO%d/SCL GPIO%d at 0x%02x: %s",
                 OLED_SDA_GPIO, OLED_SCL_GPIO, OLED_ADDR, esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "OLED status display started on SDA GPIO%d, SCL GPIO%d", OLED_SDA_GPIO, OLED_SCL_GPIO);

    while (true) {
        runtime_state_t runtime = pump_controller_get_runtime();
        moisture_reading_t moisture = moisture_manager_get_latest();
        char line[32];

        fb_clear();
        draw_text(0, 0, "WATERING SYSTEM");
        snprintf(line, sizeof(line), "PUMP:%s", short_pump_state(runtime.pump_state));
        draw_text(0, 2, line);
        snprintf(line, sizeof(line), "LEFT:%lus", (unsigned long)runtime.current_run_remaining_sec);
        draw_text(0, 3, line);
        snprintf(line, sizeof(line), "RES:%s", runtime.reservoir_ok ? "OK" : "LOW");
        draw_text(0, 4, line);
        if (moisture.enabled) snprintf(line, sizeof(line), "MOIST:%d%%", moisture.percent);
        else snprintf(line, sizeof(line), "MOIST:OFF");
        draw_text(0, 5, line);
        snprintf(line, sizeof(line), "NEXT:%s", scheduler_next_run_text());
        draw_text(0, 7, line);
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(OLED_REFRESH_MS));
    }
}

esp_err_t status_display_start(void)
{
    BaseType_t ok = xTaskCreate(status_display_task, "Status Display", 4096, NULL, 3, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
