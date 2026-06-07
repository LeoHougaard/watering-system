#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    char ssid[33];
    char password[65];
    bool configured;
} wifi_provisioning_credentials_t;

esp_err_t wifi_provisioning_start(void);
esp_err_t wifi_provisioning_get_credentials(wifi_provisioning_credentials_t *credentials);
esp_err_t wifi_provisioning_save_credentials(const wifi_provisioning_credentials_t *credentials);
