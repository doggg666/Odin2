#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_lvgl_port.h"
#include "odin_board.h"
#include "odin_ui.h"
#include "odin_wifi_ui.h"
#include "odin_ai.h"
#include "odin_audio.h"
#include "odin_settings.h"
#include "nvs_flash.h"

static const char *TAG = "odin";

void app_main(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "Odin CrowPanel V1.0 starting; chip %s, revision %d",
             CONFIG_IDF_TARGET, chip_info.revision);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(odin_audio_settings_init());
    ESP_ERROR_CHECK(odin_board_init());
    if (!lvgl_port_lock(1000)) {
        ESP_LOGE(TAG, "Timed out acquiring LVGL lock");
        ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
    }
    odin_ui_create();
    odin_ui_set_status("Ready when you are");
    odin_ui_set_mode("OFFLINE");
    odin_ui_set_transcript("Ask a question, share a thought,\nor simply start talking.");
    odin_wifi_ui_create();
    odin_ai_create();
    odin_settings_create();
    lvgl_port_unlock();
    ESP_ERROR_CHECK(odin_board_set_backlight(100));
    ESP_LOGI(TAG, "Odin UI ready; display and touch initialized");
}
