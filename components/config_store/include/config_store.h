#pragma once

#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t config_store_init(void);
esp_err_t config_store_get_settings(system_settings_t *settings);
esp_err_t config_store_save_settings(const system_settings_t *settings);
esp_err_t config_store_get_planters(planter_config_t planters[PLANTER_COUNT]);
esp_err_t config_store_save_planter(const planter_config_t *planter);
esp_err_t config_store_save_planters(const planter_config_t planters[PLANTER_COUNT]);
