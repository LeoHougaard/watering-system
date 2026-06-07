#pragma once

#include "esp_err.h"

esp_err_t recommendation_engine_start(void);
char *recommendation_engine_get_json(void);
esp_err_t recommendation_engine_resolve(const char *id);
esp_err_t recommendation_engine_snooze(const char *id, int hours);
