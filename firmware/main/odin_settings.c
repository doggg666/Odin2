#include "odin_settings.h"
#include "odin_ui.h"
#include "odin_audio.h"
#include "odin_wifi_ui.h"
#include "odin_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

typedef struct {
    esp_err_t error;
    odin_battery_info_t battery;
} battery_result_t;
static QueueHandle_t readings;
static lv_obj_t *page, *battery_value, *battery_details, *battery_note;
static lv_obj_t *volume_slider, *volume_value;

static const char *charge_name(unsigned value)
{
    static const char *names[] = {"Idle", "Charging", "Fully charged", "Not charging", "Charge error"};
    return value < 5 ? names[value] : "Unknown";
}
static void battery_worker(void *arg)
{
    bool had_reading = false;
    esp_err_t previous_error = ESP_OK;
    for (;;) {
        battery_result_t result = {0};
        result.error = odin_board_read_battery(&result.battery);
        if (result.error == ESP_OK && (!had_reading || previous_error != ESP_OK)) {
            ESP_LOGI("odin_battery", "STC8 battery: %lu mV, %u%%, charge state %u, ADC %lu mV",
                     (unsigned long)result.battery.battery_mv, result.battery.percent,
                     result.battery.charge_state, (unsigned long)result.battery.adc_mv);
            had_reading = true;
        } else if (result.error != ESP_OK && (!had_reading || previous_error != result.error)) {
            ESP_LOGW("odin_battery", "Battery status unavailable: %s", esp_err_to_name(result.error));
            had_reading = true;
        }
        previous_error = result.error;
        xQueueOverwrite(readings, &result);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
static void home_cb(lv_event_t *e) { lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN); }
static void open_cb(lv_event_t *e)
{
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page);
}
static void wifi_cb(lv_event_t *e)
{
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    odin_wifi_ui_open();
}
static void volume_cb(lv_event_t *e)
{
    unsigned value = lv_slider_get_value(volume_slider);
    odin_audio_set_volume(value);
    if (value) lv_label_set_text_fmt(volume_value, "%u%%", value);
    else lv_label_set_text(volume_value, "Muted");
    if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST)
        odin_audio_save_volume();
}
static void poll(lv_timer_t *timer)
{
    battery_result_t result;
    if (xQueueReceive(readings, &result, 0)) {
        bool available = result.error == ESP_OK;
        odin_ui_set_battery(available, result.battery.battery_mv,
                            result.battery.percent, result.battery.charge_state == 1);
        if (available) {
            lv_label_set_text_fmt(battery_value, "%lu.%02lu V", (unsigned long)(result.battery.battery_mv / 1000),
                                  (unsigned long)((result.battery.battery_mv % 1000) / 10));
            if (result.battery.battery_mv > 4350) {
                lv_label_set_text(battery_details, "Reading needs checking");
                lv_label_set_text(battery_note, "Reported voltage is high for one cell. Check whether a battery is connected.");
                lv_obj_set_style_text_color(battery_value, lv_color_hex(0xF1B5A5), 0);
            } else {
                lv_label_set_text_fmt(battery_details, "%u%% remaining\n%s", result.battery.percent,
                                      charge_name(result.battery.charge_state));
                lv_label_set_text(battery_note, "Battery percentage is an estimate from the onboard controller.");
                lv_obj_set_style_text_color(battery_value, lv_color_hex(ODIN_ACCENT), 0);
            }
        } else {
            lv_label_set_text(battery_value, "Unavailable");
            lv_label_set_text(battery_details, "No valid battery reading");
            lv_label_set_text(battery_note, "Battery information will refresh automatically when available.");
        }
    }
    unsigned volume = odin_audio_get_volume();
    if (lv_slider_get_value(volume_slider) != volume) {
        lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
        if (volume) lv_label_set_text_fmt(volume_value, "%u%%", volume);
        else lv_label_set_text(volume_value, "Muted");
    }
}
static lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent); lv_label_set_text(obj, value);
    lv_obj_set_pos(obj, x, y); lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0); return obj;
}
static lv_obj_t *button(lv_obj_t *parent, const char *value, int x, int y, int w, lv_event_cb_t cb)
{
    lv_obj_t *obj = lv_btn_create(parent); lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, 52);
    odin_ui_button_style(obj, 0); lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(obj); lv_label_set_text(label, value); lv_obj_center(label); return obj;
}
static lv_obj_t *card(lv_obj_t *parent, int y, int h)
{
    lv_obj_t *obj = lv_obj_create(parent); lv_obj_set_pos(obj, 8, y); lv_obj_set_size(obj, 944, h);
    lv_obj_set_style_pad_all(obj, 0, 0); lv_obj_set_style_radius(obj, 18, 0);
    lv_obj_set_style_border_width(obj, 1, 0); lv_obj_set_style_border_color(obj, lv_color_hex(ODIN_LINE), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(ODIN_SURFACE), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE); return obj;
}
void odin_settings_create(void)
{
    readings = xQueueCreate(1, sizeof(battery_result_t));
    if (!readings) return;
    lv_obj_t *cog = button(lv_scr_act(), LV_SYMBOL_SETTINGS, 924, 24, 52, open_cb);
    lv_obj_set_style_radius(cog, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(cog, &lv_font_montserrat_24, 0);
    page = lv_obj_create(lv_scr_act()); lv_obj_set_size(page, 1024, 600); lv_obj_center(page);
    odin_ui_panel_style(page);
    text(page, "Setting", 8, 8, &lv_font_montserrat_24, ODIN_TEXT);
    text(page, "A few things, just the way you like them.", 8, 68, &lv_font_montserrat_16, ODIN_MUTED);
    button(page, LV_SYMBOL_LEFT "  Home", 844, 0, 108, home_cb);
    lv_obj_t *battery_card = card(page, 112, 200);
    text(battery_card, LV_SYMBOL_BATTERY_FULL "  Battery", 24, 24, &lv_font_montserrat_18, ODIN_TEXT);
    battery_value = text(battery_card, "Checking...", 24, 68, &lv_font_montserrat_40, ODIN_ACCENT);
    battery_details = text(battery_card, "Waiting for battery status", 500, 74, &lv_font_montserrat_18, ODIN_TEXT);
    lv_obj_set_style_text_line_space(battery_details, 10, 0);
    battery_note = text(battery_card, "Readings update automatically.", 24, 156, &lv_font_montserrat_14, ODIN_MUTED);
    lv_obj_t *audio_card = card(page, 336, 172);
    text(audio_card, LV_SYMBOL_VOLUME_MAX "  Speaker volume", 24, 24, &lv_font_montserrat_18, ODIN_TEXT);
    unsigned volume = odin_audio_get_volume();
    volume_value = text(audio_card, "", 548, 24, &lv_font_montserrat_18, ODIN_ACCENT);
    if (volume) lv_label_set_text_fmt(volume_value, "%u%%", volume);
    else lv_label_set_text(volume_value, "Muted");
    volume_slider = lv_slider_create(audio_card); lv_obj_set_pos(volume_slider, 32, 94);
    lv_obj_set_size(volume_slider, 564, 10); lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF); lv_obj_set_ext_click_area(volume_slider, 18);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_PRESS_LOST, NULL);
    button(audio_card, LV_SYMBOL_WIFI "  Wi-Fi settings", 696, 74, 220, wifi_cb);
    text(audio_card, "Volume is saved when you release the slider.", 24, 132, &lv_font_montserrat_14, ODIN_MUTED);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(poll, 250, NULL);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    ESP_LOGI("odin_settings", "LVGL heap: %u%% used, %u bytes free, largest block %u",
             memory.used_pct, (unsigned)memory.free_size, (unsigned)memory.free_biggest_size);
    if (xTaskCreate(battery_worker, "odin_battery", 4096, NULL, 2, NULL) != pdPASS)
        lv_label_set_text(battery_details, "Battery monitoring could not start");
}
