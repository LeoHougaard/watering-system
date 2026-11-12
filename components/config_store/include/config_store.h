#pragma once

#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t config_store_init(void);
esp_err_t config_store_get_settings(system_settings_t *settings);
esp_err_t config_store_save_settings(const system_settings_t *settings);
bool config_store_pin_matches(const char *pin);
esp_err_t config_store_set_pin(const char *pin);
int config_store_moisture_target(void);
int config_store_target_time(void);
esp_err_t config_store_set_target_time(int minute);
esp_err_t config_store_set_moisture_target(int percent);
esp_err_t config_store_claim_target_cycle(time_t minute);
