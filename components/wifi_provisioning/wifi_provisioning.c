#include "wifi_provisioning.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";
static const char *NS = "wifi";
static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_FAIL_BIT = BIT1;
static EventGroupHandle_t g_wifi_events;
static int g_retry_count;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_retry_count < 5) {
            g_retry_count++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(g_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        g_retry_count = 0;
        ESP_LOGI(TAG, "connected to home Wi-Fi, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_provisioning_get_credentials(wifi_provisioning_credentials_t *credentials)
{
    memset(credentials, 0, sizeof(*credentials));
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;

    size_t ssid_len = sizeof(credentials->ssid);
    size_t pass_len = sizeof(credentials->password);
    esp_err_t ssid_err = nvs_get_str(nvs, "ssid", credentials->ssid, &ssid_len);
    esp_err_t pass_err = nvs_get_str(nvs, "password", credentials->password, &pass_len);
    nvs_close(nvs);

    credentials->configured = ssid_err == ESP_OK && credentials->ssid[0] != '\0';
    return pass_err == ESP_ERR_NVS_NOT_FOUND ? ssid_err : (ssid_err == ESP_OK ? pass_err : ssid_err);
}

esp_err_t wifi_provisioning_save_credentials(const wifi_provisioning_credentials_t *credentials)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NS, NVS_READWRITE, &nvs));
    esp_err_t err = nvs_set_str(nvs, "ssid", credentials->ssid);
    if (err == ESP_OK) err = nvs_set_str(nvs, "password", credentials->password);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

static esp_err_t start_ap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    snprintf((char *)ap.ap.ssid, sizeof(ap.ap.ssid), "IrrigationController");
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    snprintf((char *)ap.ap.password, sizeof(ap.ap.password), "water1234");
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "setup AP started: %s", ap.ap.ssid);
    return ESP_OK;
}

static esp_err_t start_sta(const wifi_provisioning_credentials_t *credentials)
{
    g_wifi_events = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, credentials->ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, credentials->password, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    EventBits_t bits = xEventGroupWaitBits(g_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & WIFI_CONNECTED_BIT) return ESP_OK;

    ESP_LOGW(TAG, "could not connect to saved Wi-Fi; falling back to setup AP");
    ESP_ERROR_CHECK(esp_wifi_stop());
    vEventGroupDelete(g_wifi_events);
    g_wifi_events = NULL;
    return start_ap();
}

esp_err_t wifi_provisioning_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_provisioning_credentials_t credentials;
    esp_err_t err = wifi_provisioning_get_credentials(&credentials);
    if (err == ESP_OK && credentials.configured) {
        ESP_LOGI(TAG, "connecting to saved Wi-Fi SSID: %s", credentials.ssid);
        return start_sta(&credentials);
    }

    wifi_provisioning_credentials_t default_credentials = {
        .ssid = "Canyon",
        .password = "927canyon",
        .configured = true,
    };
    ESP_LOGI(TAG, "connecting to default Wi-Fi SSID: %s", default_credentials.ssid);
    return start_sta(&default_credentials);
}
