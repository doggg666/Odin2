#include "odin_power.h"
#include "odin_board.h"
#include "lvgl.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>

static atomic_bool enabled;
static bool initialized, asleep, consume_touch;
static bool persisted_enabled;
static uint32_t wake_brightness = 100;
static TaskHandle_t settings_task;

bool odin_power_is_enabled(void) { return atomic_load(&enabled); }
static esp_err_t wake_screen(void)
{
    if (!asleep) return ESP_OK;
    esp_err_t err = odin_board_set_backlight(wake_brightness);
    if (err == ESP_OK) {
        asleep = false;
        lv_disp_trig_activity(NULL);
        ESP_LOGI("odin_power", "Screen awake");
    } else ESP_LOGW("odin_power", "Unable to wake screen: %s", esp_err_to_name(err));
    return err;
}
static bool filter_touch(bool pressed)
{
    if (!pressed) { consume_touch = false; return false; }
    if (asleep) {
        wake_screen();
        consume_touch = true;
    }
    if (consume_touch) {
        /* The first gesture wakes only; no hidden button or recording starts. */
        lv_disp_trig_activity(NULL);
        return true;
    }
    return false;
}
static void save_settings(void *arg)
{
    bool saved = persisted_enabled;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        bool value = odin_power_is_enabled();
        if (value == saved) continue;
        nvs_handle_t handle;
        esp_err_t err = nvs_open("odin_power", NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_u8(handle, "enabled", value);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
        }
        if (err == ESP_OK) {
            saved = value;
            ESP_LOGI("odin_power", "Power saver setting saved: %s", value ? "enabled" : "disabled");
        } else ESP_LOGW("odin_power", "Unable to save power saver: %s", esp_err_to_name(err));
    }
}
void odin_power_set_enabled(bool value)
{
    if (value == odin_power_is_enabled()) return;
    atomic_store(&enabled, value);
    if (!value) wake_screen();
    /* Enabling starts a fresh ten-minute window, even after a long idle. */
    lv_disp_trig_activity(NULL);
    if (settings_task) xTaskNotifyGive(settings_task);
}
static void idle_tick(lv_timer_t *timer)
{
    if (!odin_power_is_enabled() || asleep || lv_disp_get_inactive_time(NULL) < ODIN_POWER_IDLE_MS) return;
    uint32_t brightness = odin_board_get_backlight();
    if (brightness) wake_brightness = brightness;
    esp_err_t err = odin_board_set_backlight(0);
    if (err == ESP_OK) {
        asleep = true;
        ESP_LOGI("odin_power", "Screen off after 10 minutes without touch");
    } else ESP_LOGW("odin_power", "Unable to turn off screen: %s", esp_err_to_name(err));
}
esp_err_t odin_power_init(void)
{
    if (initialized) return ESP_OK;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("odin_power", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint8_t value;
        err = nvs_get_u8(handle, "enabled", &value);
        nvs_close(handle);
        if (err == ESP_OK && value <= 1) atomic_store(&enabled, value != 0);
        else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    persisted_enabled = odin_power_is_enabled();
    if (xTaskCreate(save_settings, "power_settings", 3072, NULL, 2, &settings_task) != pdPASS)
        return ESP_ERR_NO_MEM;
    if (!lv_timer_create(idle_tick, 500, NULL)) return ESP_ERR_NO_MEM;
    odin_board_set_touch_filter(filter_touch);
    lv_disp_trig_activity(NULL);
    initialized = true;
    ESP_LOGI("odin_power", "Power saver loaded: %s; idle timeout 600 seconds", odin_power_is_enabled() ? "enabled" : "disabled");
    return ESP_OK;
}
