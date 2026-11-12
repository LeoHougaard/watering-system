#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t scheduler_start(void);
void scheduler_refresh_next_run(void);
const char *scheduler_next_run_text(void);
void scheduler_cancel_target_cycle(void);
const char *scheduler_target_status(void);
bool scheduler_target_cycle_active(void);
