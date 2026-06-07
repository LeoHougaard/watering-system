#include "ota_update.h"

#include <stdlib.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "history_store.h"

static const char *TAG = "ota";
static const size_t OTA_CHUNK_SIZE = 4096;

esp_err_t ota_update_init(void)
{
    history_store_log_diagnostic("OTA partitioning and upload/apply handler are enabled.");
    ESP_LOGI(TAG, "OTA-ready partitions active");
    return ESP_OK;
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

esp_err_t ota_update_apply_http_request(httpd_req_t *req)
{
    if (req->content_len == 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"firmware binary body required\"}");
        return ESP_FAIL;
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"no OTA partition available\"}");
        return ESP_FAIL;
    }
    if (req->content_len > next->size) {
        ESP_LOGE(TAG, "firmware image too large: %d bytes for partition %lu", req->content_len, (unsigned long)next->size);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_sendstr(req, "{\"error\":\"firmware image too large for OTA partition\"}");
        return ESP_FAIL;
    }

    char *buffer = malloc(OTA_CHUNK_SIZE);
    if (!buffer) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"not enough memory for OTA buffer\"}");
        return ESP_ERR_NO_MEM;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(next, OTA_SIZE_UNKNOWN, &handle);
    if (err != ESP_OK) {
        free(buffer);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"failed to begin OTA\"}");
        return err;
    }

    int remaining = req->content_len;
    int total_received = 0;
    ESP_LOGI(TAG, "starting OTA upload: %d bytes to %s", req->content_len, next->label);
    while (remaining > 0) {
        int to_read = remaining > (int)OTA_CHUNK_SIZE ? (int)OTA_CHUNK_SIZE : remaining;
        int received = httpd_req_recv(req, buffer, to_read);
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (received <= 0) {
            err = ESP_FAIL;
            ESP_LOGE(TAG, "firmware upload receive failed");
            break;
        }

        err = esp_ota_write(handle, buffer, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            break;
        }
        remaining -= received;
        total_received += received;
    }
    ESP_LOGI(TAG, "OTA upload received %d/%d bytes", total_received, req->content_len);

    free(buffer);

    if (err == ESP_OK) {
        err = esp_ota_end(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        }
    } else {
        esp_ota_abort(handle);
    }

    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(next);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        }
    }

    if (err != ESP_OK) {
        history_store_log_diagnostic("OTA update failed.");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":\"OTA update failed\"}");
        return err;
    }

    history_store_log_diagnostic("OTA update applied; rebooting into new firmware.");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate(restart_task, "ota_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}
