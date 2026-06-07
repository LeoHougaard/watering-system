#include "history_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "history_store";

static const char *path_for_kind(const char *kind)
{
    if (strcmp(kind, "watering") == 0) return "/spiffs/watering.jsonl";
    if (strcmp(kind, "reservoir") == 0) return "/spiffs/reservoir.jsonl";
    if (strcmp(kind, "moisture") == 0) return "/spiffs/moisture.jsonl";
    if (strcmp(kind, "diagnostics") == 0) return "/spiffs/diagnostics.jsonl";
    if (strcmp(kind, "recommendations") == 0) return "/spiffs/recommendations.jsonl";
    if (strcmp(kind, "observations") == 0) return "/spiffs/observations.jsonl";
    return "/spiffs/watering.jsonl";
}

static esp_err_t append_json(const char *kind, cJSON *json)
{
    char *line = cJSON_PrintUnformatted(json);
    if (!line) return ESP_ERR_NO_MEM;
    FILE *f = fopen(path_for_kind(kind), "a");
    if (!f) {
        free(line);
        return ESP_FAIL;
    }
    fprintf(f, "%s\n", line);
    fclose(f);
    free(line);
    return ESP_OK;
}

esp_err_t history_store_init(void)
{
    ESP_LOGI(TAG, "flash history logs ready");
    return ESP_OK;
}

esp_err_t history_store_log_watering(const watering_event_t *e)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)e->timestamp);
    cJSON_AddStringToObject(root, "type", event_type_to_string(e->type));
    cJSON_AddNumberToObject(root, "duration_sec", e->duration_sec);
    cJSON_AddBoolToObject(root, "reservoir_ok_at_start", e->reservoir_ok_at_start);
    cJSON_AddBoolToObject(root, "completed", e->completed);
    cJSON_AddStringToObject(root, "stopped_reason", stop_reason_to_string(e->stopped_reason));
    esp_err_t err = append_json("watering", root);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_reservoir(bool reservoir_ok)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddBoolToObject(root, "reservoir_ok", reservoir_ok);
    esp_err_t err = append_json("reservoir", root);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_moisture(int percent, int raw, int gpio)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddNumberToObject(root, "percent", percent);
    cJSON_AddNumberToObject(root, "raw", raw);
    cJSON_AddNumberToObject(root, "gpio", gpio);
    esp_err_t err = append_json("moisture", root);
    cJSON_Delete(root);
    return err;
}

esp_err_t history_store_log_diagnostic(const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddStringToObject(root, "message", message);
    esp_err_t err = append_json("diagnostics", root);
    cJSON_Delete(root);
    return err;
}

char *history_store_read_json_array(const char *kind)
{
    FILE *f = fopen(path_for_kind(kind), "r");
    cJSON *array = cJSON_CreateArray();
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            cJSON *item = cJSON_Parse(line);
            if (item) cJSON_AddItemToArray(array, item);
        }
        fclose(f);
    }
    char *out = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    return out;
}
