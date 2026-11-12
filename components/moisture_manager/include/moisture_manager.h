#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "esp_err.h"

typedef enum {
    MOISTURE_DISABLED, MOISTURE_WARMING, MOISTURE_VALID, MOISTURE_SATURATED,
    MOISTURE_READ_ERROR, MOISTURE_INVALID_CONFIG, MOISTURE_STALE
} moisture_quality_t;

typedef struct {
    moisture_quality_t quality;
    int64_t sampled_at_us;
    uint32_t dry_raw, wet_raw;
    bool enabled;
    int gpio;
    int raw;
    int percent;
    uint32_t samples_collected;
    time_t updated_at;
} moisture_reading_t;

esp_err_t moisture_manager_start(void);
moisture_reading_t moisture_manager_get_latest(void);

const char *moisture_quality_name(moisture_quality_t quality);
