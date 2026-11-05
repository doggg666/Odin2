#include "odin_clock.h"
#include "odin_time.h"
#include "odin_wifi.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static bool started;
static void synchronized(struct timeval *time)
{
    char text[96];
    if (odin_time_format(time->tv_sec, text, sizeof(text)))
        ESP_LOGI("odin_clock", "Time synchronized: %s", text);
}
static void clock_worker(void *arg)
{
    for (;;) {
        while (!odin_wifi_is_connected()) vTaskDelay(pdMS_TO_TICKS(500));
        esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
            2, ESP_SNTP_SERVER_LIST("time.nrc.ca", "pool.ntp.org"));
        cfg.wait_for_sync = false;
        cfg.sync_cb = synchronized;
        esp_err_t err = esp_netif_sntp_init(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI("odin_clock", "Network clock started for Montreal, Quebec");
            break;
        }
        ESP_LOGW("odin_clock", "Time service initialization failed: %s; retrying", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    vTaskDelete(NULL);
}
esp_err_t odin_clock_init(void)
{
    if (started) return ESP_OK;
    if (!odin_time_configure()) return ESP_ERR_NO_MEM;
    if (xTaskCreate(clock_worker, "odin_clock", 4096, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    started = true;
    return ESP_OK;
}
