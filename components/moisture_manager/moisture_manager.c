#include "moisture_manager.h"

#include <stdbool.h>
#include <stdlib.h>

#include "config_store.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"

static const char *TAG = "moisture";

static moisture_reading_t g_latest = {
    .enabled = false,
    .gpio = -1,
    .raw = 0,
    .percent = -1,
};

static int clamp_percent(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

static int raw_to_percent(int raw, uint32_t dry_raw, uint32_t wet_raw)
{
    if (dry_raw == wet_raw) return 0;
    int span = (int)dry_raw - (int)wet_raw;
    int scaled = ((int)dry_raw - raw) * 100 / span;
    return clamp_percent(scaled);
}

static bool gpio_to_adc1_channel(int gpio, adc_channel_t *channel)
{
    switch (gpio) {
    case 1: *channel = ADC_CHANNEL_0; return true;
    case 2: *channel = ADC_CHANNEL_1; return true;
    case 3: *channel = ADC_CHANNEL_2; return true;
    case 4: *channel = ADC_CHANNEL_3; return true;
    case 5: *channel = ADC_CHANNEL_4; return true;
    case 6: *channel = ADC_CHANNEL_5; return true;
    case 7: *channel = ADC_CHANNEL_6; return true;
    case 8: *channel = ADC_CHANNEL_7; return true;
    case 9: *channel = ADC_CHANNEL_8; return true;
    case 10: *channel = ADC_CHANNEL_9; return true;
    default: return false;
    }
}

static void moisture_task(void *arg)
{
    (void)arg;
    system_settings_t settings;
    config_store_get_settings(&settings);
    g_latest.enabled = settings.moisture_sensor_enabled;
    g_latest.gpio = settings.moisture_gpio;

    if (!settings.moisture_sensor_enabled) {
        ESP_LOGI(TAG, "moisture sensor disabled");
        vTaskDelete(NULL);
    }

    adc_channel_t channel = ADC_CHANNEL_0;
    if (!gpio_to_adc1_channel(settings.moisture_gpio, &channel)) {
        ESP_LOGE(TAG, "GPIO %d is not an ADC1 pin on ESP32-S3", settings.moisture_gpio);
        history_store_log_diagnostic("Moisture sensor GPIO is not valid for ADC1.");
        vTaskDelete(NULL);
    }

    adc_oneshot_unit_handle_t unit = NULL;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    if (adc_oneshot_new_unit(&init_config, &unit) != ESP_OK) {
        history_store_log_diagnostic("Moisture ADC init failed.");
        vTaskDelete(NULL);
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(unit, channel, &channel_config));

    uint32_t interval = settings.moisture_sample_interval_sec;
    if (interval < 60) interval = 60;

    while (true) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(unit, channel, &raw);
        if (err == ESP_OK) {
            int percent = raw_to_percent(raw, settings.moisture_dry_raw, settings.moisture_wet_raw);
            g_latest.raw = raw;
            g_latest.percent = percent;
            history_store_log_moisture(percent, raw, settings.moisture_gpio);
            ESP_LOGI(TAG, "moisture GPIO %d raw=%d percent=%d", settings.moisture_gpio, raw, percent);
        } else {
            ESP_LOGW(TAG, "moisture read failed: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
}

esp_err_t moisture_manager_start(void)
{
    xTaskCreate(moisture_task, "Moisture Task", 4096, NULL, 4, NULL);
    return ESP_OK;
}

moisture_reading_t moisture_manager_get_latest(void)
{
    return g_latest;
}
