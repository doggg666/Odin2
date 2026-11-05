#include "odin_wifi.h"
#if __has_include("wifi_defaults.local.h")
#include "wifi_defaults.local.h"
#else
#define ODIN_DEFAULT_SSID ""
#define ODIN_DEFAULT_PASSWORD ""
#endif
#include <string.h>
#include <stdatomic.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define CONNECTED BIT0
#define FAILED BIT1
#define STOPPED BIT2
static EventGroupHandle_t events;
static bool initialized;
static atomic_bool want_connection;
static unsigned retries;
static const char *TAG = "odin_wifi";

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events, STOPPED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (want_connection) esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        if (want_connection && retries++ < 3) {
            esp_wifi_connect();
        } else if (want_connection) {
            xEventGroupSetBits(events, FAILED);
            ESP_LOGW(TAG, "Connection failed after retries");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        retries = 0;
        xEventGroupSetBits(events, CONNECTED);
        ESP_LOGI(TAG, "Wi-Fi connected; IP address acquired");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        xEventGroupClearBits(events, CONNECTED);
    }
}

esp_err_t odin_wifi_init(void)
{
    if (initialized) return ESP_OK;
    events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    if (!esp_netif_create_default_wifi_sta()) return ESP_FAIL;
    /* esp_wifi_remote initializes ESP-Hosted and the configured C6 SDIO link. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL);
    if (err != ESP_OK) return err;
    /* Save validated credentials in host NVS, not the coprocessor configuration. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    wifi_config_t empty = {0};
    err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err == ESP_OK) initialized = true;
    return err;
}

static esp_err_t stop_wifi(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    want_connection = false;
    xEventGroupClearBits(events, STOPPED | CONNECTED | FAILED);
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) return err;
    if (!(xEventGroupWaitBits(events, STOPPED, pdFALSE, pdFALSE,
                             pdMS_TO_TICKS(5000)) & STOPPED)) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t odin_wifi_disconnect(void)
{
    esp_err_t err = stop_wifi();
    return err == ESP_OK ? esp_wifi_start() : err;
}

esp_err_t odin_wifi_connect(const char *ssid, const char *password)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !password || strlen(password) > 63)
        return ESP_ERR_INVALID_ARG;
    esp_err_t err = stop_wifi();
    if (err != ESP_OK) return err;
    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, password, strlen(password));
    cfg.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    memset(&cfg, 0, sizeof(cfg));
    if (err != ESP_OK) return err;
    retries = 0;
    want_connection = true;
    err = esp_wifi_start();
    if (err != ESP_OK) { want_connection = false; return err; }
    EventBits_t bits = xEventGroupWaitBits(events, CONNECTED | FAILED, pdFALSE,
                                          pdFALSE, pdMS_TO_TICKS(35000));
    if (!(bits & CONNECTED)) {
        odin_wifi_disconnect();
        return (bits & FAILED) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }
    {
        nvs_handle_t nvs;
        err = nvs_open("odin_wifi", NVS_READWRITE, &nvs);
        if (err != ESP_OK) return err;
        err = nvs_set_str(nvs, "ssid", ssid);
        if (err == ESP_OK) err = nvs_set_str(nvs, "password", password);
        if (err == ESP_OK) err = nvs_set_u8(nvs, "configured", 1);
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
    }
    if (err == ESP_OK) ESP_LOGI(TAG, "Successful Wi-Fi credentials saved to NVS");
    return err;
}

esp_err_t odin_wifi_connect_saved(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("odin_wifi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    uint8_t configured = 0;
    err = nvs_get_u8(nvs, "configured", &configured);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        /* One-time default. Forget sets the marker so it cannot return on reboot. */
        ESP_LOGI(TAG, "Connecting to provisioned default network");
        return odin_wifi_connect(ODIN_DEFAULT_SSID, ODIN_DEFAULT_PASSWORD);
    }
    if (err != ESP_OK) { nvs_close(nvs); return err; }
    char ssid[33] = {0}, password[64] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(password);
    err = nvs_get_str(nvs, "ssid", ssid, &slen);
    if (err == ESP_OK) err = nvs_get_str(nvs, "password", password, &plen);
    nvs_close(nvs);
    if (err == ESP_OK) err = odin_wifi_connect(ssid, password);
    memset(password, 0, sizeof(password));
    return err;
}

esp_err_t odin_wifi_forget(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("odin_wifi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    /* Erase only this app's Wi-Fi credentials. Keep default provisioning disabled. */
    err = nvs_erase_all(nvs);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "configured", 1);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err != ESP_OK) return err;
    err = stop_wifi();
    if (err != ESP_OK) return err;
    wifi_config_t empty = {0};
    err = esp_wifi_set_config(WIFI_IF_STA, &empty);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err == ESP_OK) ESP_LOGI(TAG, "Saved network forgotten; automatic reconnect disabled");
    return err;
}

esp_err_t odin_wifi_scan(wifi_ap_record_t *results, uint16_t *count)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    if (!results || !count || !*count) return ESP_ERR_INVALID_ARG;
    wifi_scan_config_t cfg = {0};
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) return err;
    err = esp_wifi_scan_get_ap_records(count, results);
    if (err == ESP_OK) ESP_LOGI(TAG, "Scan completed: %u networks", *count);
    return err;
}

bool odin_wifi_is_connected(void)
{
    return events && (xEventGroupGetBits(events) & CONNECTED);
}
