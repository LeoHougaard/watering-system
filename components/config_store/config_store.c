#include "config_store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config_store";
static const char *NS = "irrigation";
static system_settings_t g_settings;
static planter_config_t g_planters[PLANTER_COUNT];

static bool normalize_settings(system_settings_t *settings)
{
    bool changed = false;
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
    if (required != size) return ESP_ERR_INVALID_SIZE;
    void *tmp = malloc(size);
    if (!tmp) return ESP_ERR_NO_MEM;
    err = nvs_get_blob(nvs, key, tmp, &required);
    if (err == ESP_OK && required == size) memcpy(value, tmp, size);
    free(tmp);
    return err == ESP_OK && required == size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t config_store_init(void)
{
    model_defaults_settings(&g_settings);
    model_defaults_planters(g_planters);

    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &nvs));
    esp_err_t s_err = load_blob_or_default(nvs, "settings", &g_settings, sizeof(g_settings));
    esp_err_t p_err = load_blob_or_default(nvs, "planters", g_planters, sizeof(g_planters));
    bool settings_changed = normalize_settings(&g_settings);
    if (s_err != ESP_OK) ESP_ERROR_CHECK(config_store_save_settings(&g_settings));
    else if (settings_changed) ESP_ERROR_CHECK(config_store_save_settings(&g_settings));
    if (p_err != ESP_OK) ESP_ERROR_CHECK(config_store_save_planters(g_planters));
    nvs_close(nvs);
    ESP_LOGI(TAG, "loaded settings and %d planter profiles", PLANTER_COUNT);
    return ESP_OK;
}

esp_err_t config_store_get_settings(system_settings_t *settings)
{
    memcpy(settings, &g_settings, sizeof(*settings));
    return ESP_OK;
}

esp_err_t config_store_save_settings(const system_settings_t *settings)
{
    g_settings = *settings;
    normalize_settings(&g_settings);
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open nvs");
    esp_err_t err = nvs_set_blob(nvs, "settings", &g_settings, sizeof(g_settings));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

esp_err_t config_store_get_planters(planter_config_t planters[PLANTER_COUNT])
{
    memcpy(planters, g_planters, sizeof(g_planters));
    return ESP_OK;
}

esp_err_t config_store_save_planter(const planter_config_t *planter)
{
    if (planter->id < 1 || planter->id > PLANTER_COUNT) return ESP_ERR_INVALID_ARG;
    g_planters[planter->id - 1] = *planter;
    return config_store_save_planters(g_planters);
}

esp_err_t config_store_save_planters(const planter_config_t planters[PLANTER_COUNT])
{
    memcpy(g_planters, planters, sizeof(g_planters));
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &nvs), TAG, "open nvs");
    esp_err_t err = nvs_set_blob(nvs, "planters", g_planters, sizeof(g_planters));
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
