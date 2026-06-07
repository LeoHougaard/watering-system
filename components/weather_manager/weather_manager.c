#include "weather_manager.h"

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

static const char *TAG = "weather";
static const char *URL =
    "http://api.open-meteo.com/v1/forecast?"
    "latitude=49.32&longitude=-123.07"
    "&current=temperature_2m,precipitation"
    "&daily=temperature_2m_max,precipitation_sum,et0_fao_evapotranspiration"
    "&past_days=1&forecast_days=3&timezone=America%2FVancouver";
static const float BASELINE_DAILY_ET0_MM = 3.0f;
static const float EFFECTIVE_RAIN_RATIO = 0.8f;
static const float RAIN_SKIP_THRESHOLD_MM = 3.0f;

typedef struct {
    char *buf;
    int len;
    int cap;
} weather_buffer_t;

static esp_err_t weather_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || !evt->user_data || !evt->data || evt->data_len <= 0) return ESP_OK;
    weather_buffer_t *wb = (weather_buffer_t *)evt->user_data;
    if (wb->len + evt->data_len + 1 > wb->cap) return ESP_FAIL;
    memcpy(wb->buf + wb->len, evt->data, evt->data_len);
    wb->len += evt->data_len;
    wb->buf[wb->len] = '\0';
    return ESP_OK;
}

static float clamp_multiplier(float value)
{
    if (value < 0.65f) return 0.65f;
    if (value > 1.25f) return 1.25f;
    return value;
}

static float json_number_at(const cJSON *array, int index, float fallback)
{
    const cJSON *item = cJSON_IsArray(array) ? cJSON_GetArrayItem(array, index) : NULL;
    return cJSON_IsNumber(item) ? (float)item->valuedouble : fallback;
}

static void update_weather_once(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    if (!settings.weather_adjust_enabled) return;

    char response[3072] = {0};
    weather_buffer_t wb = {.buf = response, .cap = sizeof(response)};
    esp_http_client_config_t config = {
        .url = URL,
        .timeout_ms = 6000,
        .event_handler = weather_http_event,
        .user_data = &wb,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status < 200 || status >= 300) {
        history_store_log_diagnostic("Weather update failed; keeping last watering adjustment.");
        ESP_LOGW(TAG, "weather update failed: %s status=%d", esp_err_to_name(err), status);
        return;
    }

    cJSON *root = cJSON_Parse(response);
    if (!root) {
        history_store_log_diagnostic("Weather response could not be parsed; keeping last watering adjustment.");
        ESP_LOGW(TAG, "weather JSON parse failed");
        return;
    }
    cJSON *current = cJSON_GetObjectItem(root, "current");
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
    cJSON *precip = cJSON_GetObjectItem(current, "precipitation");
    cJSON *temp_max_array = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON *precip_sum_array = cJSON_GetObjectItem(daily, "precipitation_sum");
    cJSON *et0_array = cJSON_GetObjectItem(daily, "et0_fao_evapotranspiration");
    if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(precip)) {
        cJSON_Delete(root);
        history_store_log_diagnostic("Weather response missed required fields; keeping last watering adjustment.");
        return;
    }

    float today_temp_max = json_number_at(temp_max_array, 1, (float)temp->valuedouble);
    float yesterday_rain = json_number_at(precip_sum_array, 0, 0.0f);
    float today_rain = json_number_at(precip_sum_array, 1, (float)precip->valuedouble);
    float tomorrow_rain = json_number_at(precip_sum_array, 2, 0.0f);
    float today_et0 = json_number_at(et0_array, 1, BASELINE_DAILY_ET0_MM);
    float tomorrow_et0 = json_number_at(et0_array, 2, today_et0);
    float forecast_rain = today_rain + tomorrow_rain;
    float effective_rain = (yesterday_rain * 0.5f + forecast_rain) * EFFECTIVE_RAIN_RATIO;
    float average_et0 = (today_et0 + tomorrow_et0) * 0.5f;
    float net_demand = average_et0 - effective_rain;

    settings.weather_last_temp_c = today_temp_max;
    settings.weather_last_precip_mm = forecast_rain;
    if (settings.weather_skip_on_rain && forecast_rain >= RAIN_SKIP_THRESHOLD_MM) {
        settings.weather_last_multiplier = 0.0f;
    } else if (forecast_rain >= 1.0f || yesterday_rain >= 2.0f) {
        settings.weather_last_multiplier = settings.weather_rain_multiplier;
    } else {
        settings.weather_last_multiplier = clamp_multiplier(net_demand / BASELINE_DAILY_ET0_MM);
        if (today_temp_max >= 28.0f && settings.weather_last_multiplier < settings.weather_hot_multiplier) {
            settings.weather_last_multiplier = settings.weather_hot_multiplier;
        }
        settings.weather_last_multiplier = clamp_multiplier(settings.weather_last_multiplier);
    }
    settings.weather_last_update = time(NULL);
    config_store_save_settings(&settings);
    ESP_LOGI(TAG, "weather multiplier %.2f temp %.1fC rain %.1fmm et0 %.1fmm",
             settings.weather_last_multiplier,
             settings.weather_last_temp_c,
             settings.weather_last_precip_mm,
             average_et0);
    cJSON_Delete(root);
}

static void weather_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(30 * 1000));
    while (true) {
        update_weather_once();
        vTaskDelay(pdMS_TO_TICKS(60 * 60 * 1000));
    }
}

esp_err_t weather_manager_start(void)
{
    xTaskCreate(weather_task, "Weather Task", 6144, NULL, 3, NULL);
    return ESP_OK;
}
