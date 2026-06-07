#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    bool bypass;
    bool sensor_enabled;
    bool active_high;
    bool ok;
    int gpio;
    int raw_level;
} reservoir_status_t;

esp_err_t reservoir_manager_start(void);
bool reservoir_manager_is_ok(void);
reservoir_status_t reservoir_manager_get_status(void);
