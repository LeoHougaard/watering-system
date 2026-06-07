#pragma once

#include "esp_err.h"
#include "irrigation_model.h"

esp_err_t history_store_init(void);
esp_err_t history_store_log_watering(const watering_event_t *event);
esp_err_t history_store_log_reservoir(bool reservoir_ok);
esp_err_t history_store_log_moisture(int percent, int raw, int gpio);
esp_err_t history_store_log_diagnostic(const char *message);
char *history_store_read_json_array(const char *kind);
