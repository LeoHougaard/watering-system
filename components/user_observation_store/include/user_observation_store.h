#pragma once

#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t user_observation_store_init(void);
esp_err_t user_observation_store_add(const user_observation_t *observation);
char *user_observation_store_read_json(void);
