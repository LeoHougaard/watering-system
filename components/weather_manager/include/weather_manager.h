#pragma once

#include "esp_err.h"

esp_err_t weather_manager_start(void);
void weather_manager_request_refresh(void);
