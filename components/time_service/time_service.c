#include "time_service.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "time";

esp_err_t time_service_init(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    const char *tz = strcmp(settings.timezone, "America/Vancouver") == 0 ? "PST8PDT,M3.2.0,M11.1.0" : settings.timezone;
    setenv("TZ", tz, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "timezone set to %s; SNTP started", tz);
    return ESP_OK;
}
