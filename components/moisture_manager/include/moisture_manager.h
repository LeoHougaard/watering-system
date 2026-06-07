#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool enabled;
    int gpio;
    int raw;
    int percent;
} moisture_reading_t;

esp_err_t moisture_manager_start(void);
moisture_reading_t moisture_manager_get_latest(void);
