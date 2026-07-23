#include "weather_manager.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "config_store.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"
#include "irrigation_model.h"

static const char *TAG = "weather";
static const char *URL =
    "http://api.open-meteo.com/v1/forecast?"
    "latitude=49.32&longitude=-123.07"
    "&current=temperature_2m"
    "&hourly=temperature_2m,precipitation,precipitation_probability,et0_fao_evapotranspiration"
    "&past_hours=24&forecast_hours=36"
    "&timezone=America%2FVancouver&timeformat=unixtime";

#define WEATHER_RESPONSE_BYTES 4096
#define WEATHER_REFRESH_MS (3 * 60 * 60 * 1000)
#define WEATHER_RETRY_MIN_MS (15 * 60 * 1000)
#define WEATHER_RETRY_MAX_MS (60 * 60 * 1000)

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool overflowed;
} weather_buffer_t;

static TaskHandle_t g_weather_task;

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) return ESP_OK;
    weather_buffer_t *wb = (weather_buffer_t *)evt->user_data;
    if (wb->len + (size_t)evt->data_len + 1 > wb->cap) {
        wb->overflowed = true;
        return ESP_FAIL;
    }
    memcpy(wb->buf + wb->len, evt->data, evt->data_len);
    wb->len += (size_t)evt->data_len;
    wb->buf[wb->len] = '\0';
    return ESP_OK;
}

static bool json_float_at(const cJSON *array, int index, float *value)
{
    const cJSON *item = cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return false;
    *value = (float)item->valuedouble;
    return true;
}

static bool json_time_at(const cJSON *array, int index, time_t *value)
{
    const cJSON *item = cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0) return false;
    *value = (time_t)item->valuedouble;
    return true;
}

static void log_failure(const char *message)
{
    ESP_LOGW(TAG, "%s", message);
    history_store_log_diagnostic(message);
}

static bool update_weather_once(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    if (!settings.weather_adjust_enabled) return true;

    char *response = calloc(1, WEATHER_RESPONSE_BYTES);
    if (!response) {
        log_failure("Weather update ran out of memory; stale data will not be used.");
        return false;
    }
    weather_buffer_t wb = {.buf = response, .cap = WEATHER_RESPONSE_BYTES};
    esp_http_client_config_t config = {
        .url = URL,
        .timeout_ms = 8000,
        .event_handler = weather_http_event,
        .user_data = &wb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(response);
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300 || wb.overflowed) {
        ESP_LOGW(TAG, "update failed: %s status=%d bytes=%u", esp_err_to_name(err), status, (unsigned)wb.len);
        log_failure("Weather update failed; watering will use the normal duration if data becomes stale.");
        free(response);
        return false;
    }

    cJSON *root = cJSON_Parse(response);
    free(response);
    if (!root) {
        log_failure("Weather response could not be parsed; stale data will not be used.");
        return false;
    }

    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON *current_time = cJSON_GetObjectItemCaseSensitive(current, "time");
    const cJSON *current_temp = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    const cJSON *times = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    const cJSON *temperatures = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    const cJSON *precipitation = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation");
    const cJSON *probabilities = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
    const cJSON *et0 = cJSON_GetObjectItemCaseSensitive(hourly, "et0_fao_evapotranspiration");
    int count = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
    if (!cJSON_IsNumber(current_time) || !cJSON_IsNumber(current_temp) || count < 36 ||
        !cJSON_IsArray(precipitation) || !cJSON_IsArray(probabilities) || !cJSON_IsArray(et0)) {
        cJSON_Delete(root);
        log_failure("Weather response missed required hourly fields; stale data will not be used.");
        return false;
    }

    time_t observed_at = (time_t)current_time->valuedouble;
    weather_observation_t observation = {0};
    float peak_temp = (float)current_temp->valuedouble;
    float probability_weight = 0.0f;
    float weighted_probability = 0.0f;
    for (int i = 0; i < count; i++) {
        time_t sample_time;
        float rain = 0.0f, probability = 0.0f, sample_et0 = 0.0f, temperature = peak_temp;
        if (!json_time_at(times, i, &sample_time)) continue;
        json_float_at(precipitation, i, &rain);
        json_float_at(probabilities, i, &probability);
        json_float_at(et0, i, &sample_et0);
        json_float_at(temperatures, i, &temperature);
        if (rain < 0.0f) rain = 0.0f;
        if (sample_et0 < 0.0f) sample_et0 = 0.0f;

        if (sample_time <= observed_at && sample_time > observed_at - 24 * 60 * 60) {
            observation.recent_rain_mm += rain;
            observation.previous_et0_mm += sample_et0;
        } else if (sample_time > observed_at && sample_time <= observed_at + 24 * 60 * 60) {
            observation.forecast_rain_24h_mm += rain;
            observation.forecast_et0_mm += sample_et0;
            if (temperature > peak_temp) peak_temp = temperature;
            if (sample_time <= observed_at + 12 * 60 * 60) {
                observation.forecast_rain_12h_mm += rain;
                if (rain > 0.0f) {
                    weighted_probability += rain * probability;
                    probability_weight += rain;
                }
            }
        }
    }
    observation.forecast_rain_probability_pct =
        probability_weight > 0.0f ? weighted_probability / probability_weight : 0.0f;
    weather_adjustment_t adjustment = model_weather_adjustment(&observation, settings.weather_skip_on_rain);

    /* Reload after the network request so an unrelated settings change made
       while fetching is not overwritten by this background task. */
    config_store_get_settings(&settings);
    if (!settings.weather_adjust_enabled) {
        cJSON_Delete(root);
        return true;
    }
    settings.weather_last_multiplier = adjustment.multiplier;
    settings.weather_last_temp_c = peak_temp;
    settings.weather_last_precip_mm = observation.forecast_rain_24h_mm;
    settings.weather_last_et0_mm = adjustment.demand_et0_mm;
    settings.weather_last_recent_rain_mm = observation.recent_rain_mm;
    settings.weather_last_forecast_rain_mm = observation.forecast_rain_24h_mm;
    settings.weather_last_rain_probability_pct = observation.forecast_rain_probability_pct;
    settings.weather_data_version = WEATHER_DATA_VERSION;
    settings.weather_last_update = observed_at;
    err = config_store_save_settings(&settings);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        log_failure("Weather data was received but could not be saved.");
        return false;
    }

    ESP_LOGI(TAG, "%.0f%% duration, ET0 %.2fmm, recent rain %.2fmm, next 24h %.2fmm (%.0f%% confidence)%s",
             adjustment.multiplier * 100.0f,
             adjustment.demand_et0_mm,
             observation.recent_rain_mm,
             observation.forecast_rain_24h_mm,
             observation.forecast_rain_probability_pct,
             adjustment.skipped_for_rain ? ", rain skip" : "");
    return true;
}

static void weather_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(15 * 1000));
    unsigned failures = 0;
    while (true) {
        system_settings_t settings;
        config_store_get_settings(&settings);
        TickType_t wait_ticks = portMAX_DELAY;
        if (settings.weather_adjust_enabled) {
            if (update_weather_once()) {
                failures = 0;
                wait_ticks = pdMS_TO_TICKS(WEATHER_REFRESH_MS);
            } else {
                failures++;
                uint32_t retry_ms = WEATHER_RETRY_MIN_MS << (failures > 2 ? 2 : failures - 1);
                if (retry_ms > WEATHER_RETRY_MAX_MS) retry_ms = WEATHER_RETRY_MAX_MS;
                wait_ticks = pdMS_TO_TICKS(retry_ms);
            }
        } else {
            failures = 0;
        }
        ulTaskNotifyTake(pdTRUE, wait_ticks);
    }
}

esp_err_t weather_manager_start(void)
{
    return xTaskCreate(weather_task, "Weather Task", 5120, NULL, 3, &g_weather_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void weather_manager_request_refresh(void)
{
    if (g_weather_task) xTaskNotifyGive(g_weather_task);
}
