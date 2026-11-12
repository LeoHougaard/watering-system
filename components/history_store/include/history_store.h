#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t history_store_init(void);
esp_err_t history_store_log_watering(const watering_event_t *event);
esp_err_t history_store_log_reservoir(bool reservoir_ok);
esp_err_t history_store_log_moisture(int percent, int raw, int gpio);
esp_err_t history_store_log_diagnostic(const char *message);
esp_err_t history_store_trim_moisture(uint32_t days_to_keep);
char *history_store_read_json_array(const char *kind);
char *history_store_read_moisture_json(uint32_t days, size_t max_points);
esp_err_t history_store_storage_info(size_t *total_bytes, size_t *used_bytes);
