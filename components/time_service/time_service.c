#include "time_service.h"

#include <stdlib.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"

static const char *TAG = "time";

esp_err_t time_service_init(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    setenv("TZ", settings.timezone, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to %s", settings.timezone);
    return ESP_OK;
}
