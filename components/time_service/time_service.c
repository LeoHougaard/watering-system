#include "time_service.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "config_store.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "time";

esp_err_t time_service_init(void)
{
    system_settings_t settings;
    config_store_get_settings(&settings);
    const char *tz = strcmp(settings.timezone, "America/Vancouver") == 0 ? "PST8PDT,M3.2.0,M11.1.0" : settings.timezone;
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "timezone set to %s", tz);

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("time.cloudflare.com", "time.google.com", "pool.ntp.org"));
    esp_err_t err = esp_netif_sntp_init(&sntp);
    if (err == ESP_OK) ESP_LOGI(TAG, "automatic network time synchronization started");
    return err;
}
