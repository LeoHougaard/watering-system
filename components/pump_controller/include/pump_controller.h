#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t pump_controller_start(void);
esp_err_t pump_controller_manual_run(uint32_t duration_sec, const char *reason);
esp_err_t pump_controller_can_manual_run(uint32_t duration_sec, char *reason, size_t reason_len);
esp_err_t pump_controller_scheduled_run(uint32_t duration_sec);
esp_err_t pump_controller_stop(stop_reason_t reason);
runtime_state_t pump_controller_get_runtime(void);
