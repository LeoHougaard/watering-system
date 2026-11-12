#include "config_store.h"

#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";
static const char *NS = "irrigation";
static system_settings_t g_settings;
static char g_initial_pin[13];
static uint8_t g_moisture_target;
static uint16_t g_target_time = UINT16_MAX;
static int64_t g_target_cycle_minute;

static void pin_hash(const char *pin, char out[65])
{
    unsigned char digest[32];
    mbedtls_sha256((const unsigned char *)pin, strlen(pin), digest, 0);
    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    out[64] = '\0';
}

static bool provision_local_pin(system_settings_t *settings)
{
    if (strlen(settings->auth_pin_hash) == 64) return false;

    static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    uint32_t random_words[12];
    esp_fill_random(random_words, sizeof(random_words));
    for (size_t i = 0; i < sizeof(g_initial_pin) - 1; i++) {
        g_initial_pin[i] = alphabet[random_words[i] % (sizeof(alphabet) - 1)];
    }
    g_initial_pin[sizeof(g_initial_pin) - 1] = '\0';
    pin_hash(g_initial_pin, settings->auth_pin_hash);
    memset(random_words, 0, sizeof(random_words));
    return true;
}

static bool normalize_settings(system_settings_t *settings)
{
    bool changed = settings->legacy_weather_adjust_enabled || settings->legacy_weather_skip_on_rain ||
        settings->legacy_rain_delay_enabled || settings->legacy_rain_delay_until != 0 ||
        settings->legacy_seasonal_multiplier != 1.0f;
    settings->legacy_weather_adjust_enabled = false;
    settings->legacy_weather_skip_on_rain = false;
    settings->legacy_rain_delay_enabled = false;
    settings->legacy_rain_delay_until = 0;
    settings->legacy_seasonal_multiplier = 1.0f;
    if (settings->moisture_average_count == 0) {
        /* Added after deployment. A zero value identifies the old, shorter
           settings blob and applies the requested five one-minute readings. */
        settings->moisture_average_count = 5;
        settings->moisture_live_interval_sec = 60;
        changed = true;
    }
    if (settings->moisture_average_count > 20) {
        settings->moisture_average_count = 20;
        changed = true;
    }
    if (settings->moisture_live_interval_sec == 0) {
        settings->moisture_live_interval_sec = 60;
        changed = true;
    }
    if (settings->moisture_live_interval_sec > 3600) {
        settings->moisture_live_interval_sec = 3600;
        changed = true;
    }
    if (settings->moisture_sample_interval_sec < 3600) {
        settings->moisture_sample_interval_sec = 3600;
        changed = true;
    }
    if (settings->moisture_history_days == 0 || settings->moisture_history_days > 365) {
        settings->moisture_history_days = 365;
        changed = true;
    }
    if (settings->manual_max_run_sec == 600) {
        settings->manual_max_run_sec = 1200;
        changed = true;
    }
    if (settings->reservoir_sensor_enabled && !settings->reservoir_sensor_bypass &&
        (settings->reservoir_gpio == settings->pump_gpio || settings->reservoir_gpio == settings->pump_gpio_b)) {
        ESP_LOGW(TAG, "disabling reservoir sensor because GPIO%d conflicts with pump outputs", settings->reservoir_gpio);
        settings->reservoir_sensor_enabled = false;
        settings->reservoir_sensor_bypass = false;
        changed = true;
    }
    return changed;
}

static esp_err_t load_blob_or_default(nvs_handle_t nvs, const char *key, void *value, size_t size)
{
    size_t required = 0;
    esp_err_t err = nvs_get_blob(nvs, key, NULL, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NVS_NOT_FOUND;
    if (err != ESP_OK) return err;
    if (required > size) return ESP_ERR_INVALID_SIZE;
    void *tmp = malloc(size);
    if (!tmp) return ESP_ERR_NO_MEM;
    err = nvs_get_blob(nvs, key, tmp, &required);
    if (err == ESP_OK && required <= size) memcpy(value, tmp, required);
    free(tmp);
    return err == ESP_OK && required <= size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t config_store_init(void)
{
    model_defaults_settings(&g_settings);

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &nvs));
    nvs_get_u8(nvs, "moist_target", &g_moisture_target);
    nvs_get_u16(nvs, "target_time", &g_target_time);
    if (g_moisture_target > 99) g_moisture_target = 0;
    nvs_get_i64(nvs, "target_minute", &g_target_cycle_minute);
    size_t stored_settings_size = 0;
    nvs_get_blob(nvs, "settings", NULL, &stored_settings_size);
    esp_err_t s_err = load_blob_or_default(nvs, "settings", &g_settings, sizeof(g_settings));
    bool settings_changed = normalize_settings(&g_settings);
    if (s_err == ESP_OK && stored_settings_size <
        offsetof(system_settings_t, moisture_average_count) + sizeof(g_settings.moisture_average_count)) {
        g_settings.moisture_average_count = 5;
        g_settings.moisture_live_interval_sec = 60;
        settings_changed = true;
    }
    if (s_err == ESP_OK && stored_settings_size <
        offsetof(system_settings_t, storage_policy_version) + sizeof(g_settings.storage_policy_version)) {
        /* One hourly point for 365 days fits the reserved flash budget. This
           marker applies the policy once without changing later choices. */
        g_settings.moisture_history_days = 365;
        g_settings.moisture_sample_interval_sec = 3600;
        g_settings.storage_policy_version = 1;
        settings_changed = true;
    }
    bool pin_created = false;
    if (s_err == ESP_OK && strlen(g_settings.auth_pin_hash) != 64) {
        /* Keep existing deployed browsers working through this OTA. New
           controllers still receive a random per-device PIN on first boot. */
        pin_hash("1234", g_settings.auth_pin_hash);
        settings_changed = true;
        ESP_LOGW(TAG, "migrated deployed controller with its existing local PIN");
    } else {
        pin_created = provision_local_pin(&g_settings);
    }
    settings_changed |= pin_created;
    if (s_err != ESP_OK) ESP_ERROR_CHECK(config_store_save_settings(&g_settings));
    else if (settings_changed) ESP_ERROR_CHECK(config_store_save_settings(&g_settings));
    if (pin_created) {
        ESP_ERROR_CHECK(nvs_set_str(nvs, "setup_pin", g_initial_pin));
        ESP_ERROR_CHECK(nvs_commit(nvs));
    } else {
        size_t pin_len = sizeof(g_initial_pin);
        if (nvs_get_str(nvs, "setup_pin", g_initial_pin, &pin_len) != ESP_OK) g_initial_pin[0] = '\0';
    }
    nvs_close(nvs);
    if (g_initial_pin[0]) {
        ESP_LOGW(TAG, "CONTROLLER PIN: %s (save it; this message stops after the PIN is used)", g_initial_pin);
    }
    return ESP_OK;
}

esp_err_t config_store_get_settings(system_settings_t *settings)
{
    memcpy(settings, &g_settings, sizeof(*settings));
    return ESP_OK;
}

esp_err_t config_store_save_settings(const system_settings_t *settings)
{
    system_settings_t saved = *settings;
    normalize_settings(&saved);
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open nvs");
    esp_err_t err = nvs_set_blob(nvs, "settings", &saved, sizeof(saved));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) g_settings = saved;
    return err;
}

/* Separate NVS keys keep the deployed settings blob compatible with rollback. */
int config_store_moisture_target(void)
{
    return g_moisture_target;
}

int config_store_target_time(void)
{
    if (g_target_time < 1440) return g_target_time;
    for (int i = 0; i < MAX_SCHEDULE_SLOTS; i++) {
        const schedule_slot_t *slot = &g_settings.schedule_slots[i];
        int hour, minute;
        if (slot->enabled && sscanf(slot->start, "%d:%d", &hour, &minute) == 2 &&
            hour >= 0 && hour < 24 && minute >= 0 && minute < 60) return hour * 60 + minute;
    }
    return 6 * 60;
}

esp_err_t config_store_set_target_time(int minute)
{
    if (minute < 0 || minute >= 1440) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open target time");
    esp_err_t err = nvs_set_u16(nvs, "target_time", minute);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) g_target_time = minute;
    return err;
}

esp_err_t config_store_set_moisture_target(int percent)
{
    if (percent < 0 || percent > 99) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open target");
    esp_err_t err = nvs_set_u8(nvs, "moist_target", percent);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) g_moisture_target = percent;
    return err;
}

esp_err_t config_store_claim_target_cycle(time_t minute)
{
    if (minute <= g_target_cycle_minute) return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open cycle");
    esp_err_t err = nvs_set_i64(nvs, "target_minute", minute);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) g_target_cycle_minute = minute;
    return err;
}

bool config_store_pin_matches(const char *pin)
{
    if (!pin || strlen(pin) < 4 || strlen(pin) > 31 || strlen(g_settings.auth_pin_hash) != 64) return false;
    char candidate[65];
    pin_hash(pin, candidate);
    unsigned char difference = 0;
    for (size_t i = 0; i < 64; i++) difference |= (unsigned char)(candidate[i] ^ g_settings.auth_pin_hash[i]);
    memset(candidate, 0, sizeof(candidate));
    if (difference != 0) return false;
    if (g_initial_pin[0]) {
        nvs_handle_t nvs;
        if (nvs_open(NS, NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_erase_key(nvs, "setup_pin");
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        memset(g_initial_pin, 0, sizeof(g_initial_pin));
    }
    return true;
}

esp_err_t config_store_set_pin(const char *pin)
{
    if (!pin || strlen(pin) < 4 || strlen(pin) > 31) return ESP_ERR_INVALID_ARG;
    system_settings_t updated = g_settings;
    pin_hash(pin, updated.auth_pin_hash);
    esp_err_t err = config_store_save_settings(&updated);
    if (err != ESP_OK) return err;

    nvs_handle_t nvs;
    if (nvs_open(NS, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "setup_pin");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    memset(g_initial_pin, 0, sizeof(g_initial_pin));
    return ESP_OK;
}
