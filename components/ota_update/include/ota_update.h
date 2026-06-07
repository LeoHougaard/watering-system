#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t ota_update_init(void);
esp_err_t ota_update_apply_http_request(httpd_req_t *req);
