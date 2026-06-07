#pragma once

#include "esp_err.h"

esp_err_t scheduler_start(void);
void scheduler_refresh_next_run(void);
const char *scheduler_next_run_text(void);
