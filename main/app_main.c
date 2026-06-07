#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"

#include "alarm_and_safety.h"
#include "api_server.h"
#include "config_store.h"
#include "history_store.h"
#include "moisture_manager.h"
#include "ota_update.h"
#include "pump_controller.h"
#include "recommendation_engine.h"
#include "reservoir_manager.h"
#include "scheduler.h"
#include "status_display.h"
#include "time_service.h"
#include "user_observation_store.h"
#include "weather_manager.h"
#include "wifi_provisioning.h"

static const char *TAG = "irrigation";

static void mount_storage(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = true,
    };
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    mount_storage();
    ESP_ERROR_CHECK(config_store_init());
    ESP_ERROR_CHECK(history_store_init());
    ESP_ERROR_CHECK(user_observation_store_init());
    ESP_ERROR_CHECK(wifi_provisioning_start());
    ESP_ERROR_CHECK(time_service_init());
    ESP_ERROR_CHECK(reservoir_manager_start());
    ESP_ERROR_CHECK(moisture_manager_start());
    ESP_ERROR_CHECK(pump_controller_start());
    ESP_ERROR_CHECK(scheduler_start());
    ESP_ERROR_CHECK(status_display_start());
    ESP_ERROR_CHECK(recommendation_engine_start());
    ESP_ERROR_CHECK(alarm_and_safety_start());
    ESP_ERROR_CHECK(ota_update_init());
    ESP_ERROR_CHECK(api_server_start());
    ESP_ERROR_CHECK(weather_manager_start());

    ESP_LOGI(TAG, "Irrigation controller ready. Pump is off; local UI/API started.");
}
