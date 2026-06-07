#include "reservoir_manager.h"

#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"

static const char *TAG = "reservoir";
static volatile bool g_reservoir_ok = true;
static volatile bool g_bypass = false;
static volatile bool g_sensor_enabled = true;
static volatile bool g_active_high = true;
static volatile int g_gpio = -1;
static volatile int g_raw_level = -1;

bool reservoir_manager_is_ok(void)
{
    return (g_bypass || !g_sensor_enabled) ? true : g_reservoir_ok;
}

reservoir_status_t reservoir_manager_get_status(void)
{
    reservoir_status_t status = {
        .bypass = g_bypass,
        .sensor_enabled = g_sensor_enabled,
        .active_high = g_active_high,
        .ok = reservoir_manager_is_ok(),
        .gpio = g_gpio,
        .raw_level = g_raw_level,
    };
    return status;
}

static void reservoir_task(void *arg)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    g_bypass = settings.reservoir_sensor_bypass;
    g_sensor_enabled = settings.reservoir_sensor_enabled;
    g_active_high = settings.reservoir_active_high;
    g_gpio = settings.reservoir_gpio;
    if (settings.reservoir_sensor_bypass || !settings.reservoir_sensor_enabled) {
        g_reservoir_ok = true;
    } else {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << settings.reservoir_gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }

    bool last_sample = true;
    bool debounced = true;
    bool last_bypass = g_bypass;
    bool last_active_high = g_active_high;
    int last_gpio = g_gpio;
    TickType_t changed_at = xTaskGetTickCount();
    const TickType_t debounce_ticks = pdMS_TO_TICKS(settings.reservoir_debounce_ms);

    while (true) {
        config_store_get_settings(&settings);
        g_bypass = settings.reservoir_sensor_bypass;
        g_sensor_enabled = settings.reservoir_sensor_enabled;
        g_active_high = settings.reservoir_active_high;
        g_gpio = settings.reservoir_gpio;
        if (g_bypass || !g_sensor_enabled) {
            g_reservoir_ok = true;
            last_bypass = g_bypass;
            last_active_high = g_active_high;
            last_gpio = g_gpio;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        int raw = gpio_get_level(settings.reservoir_gpio);
        g_raw_level = raw;
        bool ok = settings.reservoir_active_high ? raw == 1 : raw == 0;
        if (last_bypass != g_bypass || last_active_high != g_active_high || last_gpio != g_gpio) {
            last_sample = ok;
            debounced = ok;
            g_reservoir_ok = ok;
            changed_at = xTaskGetTickCount();
            last_bypass = g_bypass;
            last_active_high = g_active_high;
            last_gpio = g_gpio;
        }
        if (ok != last_sample) {
            last_sample = ok;
            changed_at = xTaskGetTickCount();
        }
        if (ok != debounced && (xTaskGetTickCount() - changed_at) >= debounce_ticks) {
            debounced = ok;
            g_reservoir_ok = debounced;
            history_store_log_reservoir(debounced);
            ESP_LOGW(TAG, "reservoir state changed: %s", debounced ? "ok" : "low");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t reservoir_manager_start(void)
{
    xTaskCreate(reservoir_task, "Reservoir Task", 3072, NULL, 6, NULL);
    return ESP_OK;
}
