#include "moisture_manager.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include "config_store.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"

static const char *TAG = "moisture";
#define MAX_AVERAGE_SAMPLES 20

static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
static moisture_reading_t g_latest = {
    .enabled = false,
    .gpio = -1,
    .raw = 0,
    .percent = -1,
    .samples_collected = 0,
    .updated_at = 0,
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

static void publish(moisture_reading_t reading)
{
    portENTER_CRITICAL(&g_lock);
    g_latest = reading;
    portEXIT_CRITICAL(&g_lock);
}

const char *moisture_quality_name(moisture_quality_t quality)
{
    switch (quality) {
    case MOISTURE_DISABLED: return "disabled";
    case MOISTURE_WARMING: return "warming";
    case MOISTURE_VALID: return "valid";
    case MOISTURE_SATURATED: return "saturated";
    case MOISTURE_READ_ERROR: return "read_error";
    case MOISTURE_INVALID_CONFIG: return "invalid_config";
    case MOISTURE_STALE: return "stale";
    default: return "read_error";
    }
}

static void moisture_task(void *arg)
{
    (void)arg;
    adc_oneshot_unit_handle_t unit = NULL;
    int configured_gpio = -1;
    adc_channel_t channel = ADC_CHANNEL_0;
    TickType_t last_log_tick = 0;
    int raw_samples[MAX_AVERAGE_SAMPLES] = {0};
    uint32_t average_count = 0;
    uint32_t sample_count = 0;
    uint32_t sample_index = 0;
    int64_t sample_total = 0;

    moisture_reading_t latest = {.gpio = -1, .percent = -1};
    uint32_t dry_raw = 0, wet_raw = 0;
    while (true) {
        system_settings_t settings;
        config_store_get_settings(&settings);
        latest.enabled = settings.moisture_sensor_enabled;
        latest.gpio = settings.moisture_gpio;

        if (!settings.moisture_sensor_enabled) {
            if (unit) {
                adc_oneshot_del_unit(unit);
                unit = NULL;
                configured_gpio = -1;
            }
            latest = (moisture_reading_t){.gpio = settings.moisture_gpio, .percent = -1};
            publish(latest);
            average_count = 0;
            sample_count = 0;
            sample_index = 0;
            sample_total = 0;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (!gpio_to_adc1_channel(settings.moisture_gpio, &channel) ||
            settings.moisture_dry_raw == settings.moisture_wet_raw ||
            settings.moisture_dry_raw > 4095 || settings.moisture_wet_raw > 4095) {
            ESP_LOGE(TAG, "Invalid moisture GPIO or calibration");
            history_store_log_diagnostic("Moisture GPIO or calibration is invalid.");
            latest.percent = -1;
            latest.quality = MOISTURE_INVALID_CONFIG;
            sample_count = sample_index = 0; sample_total = 0;
            publish(latest);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        if (dry_raw != settings.moisture_dry_raw || wet_raw != settings.moisture_wet_raw) {
            dry_raw = settings.moisture_dry_raw; wet_raw = settings.moisture_wet_raw;
            sample_count = sample_index = 0; sample_total = 0;
        }
        if (!unit || configured_gpio != settings.moisture_gpio) {
            latest.percent = -1;
            latest.quality = MOISTURE_READ_ERROR;
            latest.samples_collected = 0;
            publish(latest);
            if (unit) adc_oneshot_del_unit(unit);
            adc_oneshot_unit_init_cfg_t init_config = {
                .unit_id = ADC_UNIT_1,
            };
            if (adc_oneshot_new_unit(&init_config, &unit) != ESP_OK) {
                history_store_log_diagnostic("Moisture ADC init failed.");
                unit = NULL;
                configured_gpio = -1;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            adc_oneshot_chan_cfg_t channel_config = {
                .bitwidth = ADC_BITWIDTH_DEFAULT,
                .atten = ADC_ATTEN_DB_12,
            };
            esp_err_t cfg_err = adc_oneshot_config_channel(unit, channel, &channel_config);
            if (cfg_err != ESP_OK) {
                ESP_LOGW(TAG, "moisture ADC channel config failed: %s", esp_err_to_name(cfg_err));
                adc_oneshot_del_unit(unit);
                unit = NULL;
                configured_gpio = -1;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            configured_gpio = settings.moisture_gpio;
            average_count = 0;
            sample_count = 0;
            sample_index = 0;
            sample_total = 0;
            ESP_LOGI(TAG, "moisture ADC enabled on GPIO %d", configured_gpio);
        }

        uint32_t requested_average_count = settings.moisture_average_count;
        if (requested_average_count < 1) requested_average_count = 1;
        if (requested_average_count > MAX_AVERAGE_SAMPLES) requested_average_count = MAX_AVERAGE_SAMPLES;
        if (average_count != requested_average_count) {
            average_count = requested_average_count;
            sample_count = 0;
            sample_index = 0;
            sample_total = 0;
        }

        latest.dry_raw = settings.moisture_dry_raw;
        latest.wet_raw = settings.moisture_wet_raw;
        if (esp_timer_get_time() - latest.sampled_at_us > 300LL * 1000000) {
            sample_count = sample_index = 0; sample_total = 0;
        }
        int raw = 0;
        esp_err_t err = adc_oneshot_read(unit, channel, &raw);
        if (err != ESP_OK || raw <= 0 || raw >= 4095) {
            latest.raw = raw;
            latest.percent = -1;
            latest.quality = err == ESP_OK ? MOISTURE_SATURATED : MOISTURE_READ_ERROR;
            latest.samples_collected = 0;
            latest.updated_at = time(NULL);
            latest.sampled_at_us = esp_timer_get_time();
            sample_count = sample_index = 0; sample_total = 0;
            publish(latest);
            ESP_LOGW(TAG, "moisture unavailable: %s", moisture_quality_name(latest.quality));
        } else {
            if (sample_count < average_count) {
                raw_samples[sample_index] = raw;
                sample_total += raw;
                sample_count++;
            } else {
                sample_total -= raw_samples[sample_index];
                raw_samples[sample_index] = raw;
                sample_total += raw;
            }
            sample_index = (sample_index + 1) % average_count;

            int averaged_raw = (int)((sample_total + (int64_t)sample_count / 2) / sample_count);
            int percent = raw_to_percent(averaged_raw, settings.moisture_dry_raw, settings.moisture_wet_raw);
            latest.raw = averaged_raw;
            latest.quality = sample_count == average_count ? MOISTURE_VALID : MOISTURE_WARMING;
            latest.percent = latest.quality == MOISTURE_VALID ? percent : -1;
            latest.samples_collected = sample_count;
            latest.updated_at = time(NULL);
            latest.sampled_at_us = esp_timer_get_time();
            publish(latest);

            uint32_t log_interval = settings.moisture_sample_interval_sec;
            if (log_interval < 60) log_interval = 60;
            TickType_t now_tick = xTaskGetTickCount();
            if (sample_count == average_count && (last_log_tick == 0 ||
                now_tick - last_log_tick >= pdMS_TO_TICKS(log_interval * 1000))) {
                history_store_log_moisture(percent, averaged_raw, settings.moisture_gpio);
                history_store_trim_moisture(settings.moisture_history_days);
                last_log_tick = now_tick;
            }
            ESP_LOGI(TAG, "moisture GPIO %d average=%d percent=%d samples=%lu/%lu",
                     settings.moisture_gpio, averaged_raw, percent,
                     (unsigned long)sample_count, (unsigned long)average_count);
        }
        uint32_t interval = settings.moisture_live_interval_sec;
        if (interval < 1) interval = 1;
        if (interval > 3600) interval = 3600;
        vTaskDelay(pdMS_TO_TICKS(interval * 1000));
    }
}

esp_err_t moisture_manager_start(void)
{
    return xTaskCreate(moisture_task, "Moisture Task", 4096, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

moisture_reading_t moisture_manager_get_latest(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    portENTER_CRITICAL(&g_lock);
    moisture_reading_t reading = g_latest;
    portEXIT_CRITICAL(&g_lock);
    int64_t age = esp_timer_get_time() - reading.sampled_at_us;
    if (!settings.moisture_sensor_enabled) reading.quality = MOISTURE_DISABLED;
    else if (reading.gpio != settings.moisture_gpio ||
             reading.dry_raw != settings.moisture_dry_raw ||
             reading.wet_raw != settings.moisture_wet_raw) reading.quality = MOISTURE_INVALID_CONFIG;
    else if (reading.quality == MOISTURE_VALID &&
             (age < 0 || age > 300LL * 1000000)) reading.quality = MOISTURE_STALE;
    if (reading.quality != MOISTURE_VALID) reading.percent = -1;
    return reading;
}
