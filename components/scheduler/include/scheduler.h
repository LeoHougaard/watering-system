#pragma once

#include "esp_err.h"

esp_err_t scheduler_start(void);
const char *scheduler_next_run_text(void);
