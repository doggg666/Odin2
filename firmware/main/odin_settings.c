#include "odin_settings.h"
#include "odin_ui.h"
#include "odin_audio.h"
#include "odin_wifi_ui.h"
#include "odin_board.h"
#include "odin_models.h"
#include "odin_power.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

typedef struct {
    esp_err_t error;
    odin_battery_info_t battery;
} battery_result_t;
static QueueHandle_t readings;
static lv_obj_t *cog;
static lv_obj_t *page, *battery_value, *battery_details, *battery_note;
static lv_obj_t *volume_slider, *volume_value, *power_switch;
static lv_obj_t *model_selector, *model_status, *model_refresh;
static odin_models_snapshot_t models;
static uint32_t model_revision;

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
void odin_settings_set_home(bool home)
{
    if (!cog) return;
    if (home) lv_obj_clear_flag(cog, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(cog, LV_OBJ_FLAG_HIDDEN);
}
static void home_cb(lv_event_t *e)
{
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    odin_settings_set_home(true);
}
static void open_cb(lv_event_t *e)
{
    odin_settings_set_home(false);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(page);
    odin_models_refresh();
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
static void power_changed_cb(lv_event_t *event)
{
    odin_power_set_enabled(lv_obj_has_state(power_switch, LV_STATE_CHECKED));
}
static void model_refresh_cb(lv_event_t *e)
{
    if (!odin_models_refresh()) lv_label_set_text(model_status, "Model service is busy. Try again shortly.");
}
static void model_selected_cb(lv_event_t *e)
{
    unsigned index = lv_dropdown_get_selected(model_selector);
    if (index >= models.catalog.count) return;
    if (odin_models_select(models.catalog.ids[index]))
        lv_label_set_text(model_status, "Saving model selection...");
    else lv_label_set_text(model_status, "Model service is busy. Choose again shortly.");
}
static void update_models(void)
{
    if (!odin_models_snapshot(&models, &model_revision)) return;
    static char options[ODIN_MODEL_MAX * ODIN_MODEL_ID_SIZE];
    size_t used = 0;
    for (unsigned i = 0; i < models.catalog.count; ++i) {
        int written = snprintf(options + used, sizeof(options) - used, "%s%s", i ? "\n" : "", models.catalog.ids[i]);
        if (written < 0 || (size_t)written >= sizeof(options) - used) break;
        used += written;
    }
    options[used] = 0;
    lv_dropdown_set_options(model_selector, used ? options : "No models available");
    int selected = odin_model_catalog_find(&models.catalog, models.selected);
    if (selected >= 0) {
        lv_dropdown_set_selected(model_selector, selected);
        lv_dropdown_set_text(model_selector, NULL);
    } else lv_dropdown_set_text(model_selector, models.selected[0] ? "Saved model unavailable" : "Choose an AI model");
    if (models.catalog.count && !models.busy) lv_obj_clear_state(model_selector, LV_STATE_DISABLED);
    else lv_obj_add_state(model_selector, LV_STATE_DISABLED);
    if (models.busy) lv_obj_add_state(model_refresh, LV_STATE_DISABLED);
    else lv_obj_clear_state(model_refresh, LV_STATE_DISABLED);
    lv_label_set_text(model_status, models.status);
}
static void poll(lv_timer_t *timer)
{
    update_models();
    battery_result_t result;
    if (xQueueReceive(readings, &result, 0)) {
        bool available = result.error == ESP_OK;
        odin_ui_set_battery(available, result.battery.battery_mv,
                            result.battery.percent, result.battery.charge_state == 1);
        if (available) {
            lv_label_set_text_fmt(battery_value, "%lu.%02lu V", (unsigned long)(result.battery.battery_mv / 1000),
                                  (unsigned long)((result.battery.battery_mv % 1000) / 10));
            if (result.battery.battery_mv > ODIN_USB_POWER_THRESHOLD_MV) {
                lv_label_set_text(battery_details, "USB power connected");
                lv_label_set_text(battery_note, "USB power is shown when reported voltage is above 4.3 V.");
                lv_obj_set_style_text_color(battery_value, lv_color_hex(ODIN_ACCENT), 0);
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
    cog = button(lv_scr_act(), LV_SYMBOL_SETTINGS, 924, 24, 52, open_cb);
    lv_obj_set_style_radius(cog, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_font(cog, &lv_font_montserrat_24, 0);
    page = lv_obj_create(lv_scr_act()); lv_obj_set_size(page, 1024, 600); lv_obj_center(page);
    odin_ui_panel_style(page);
    text(page, "Setting", 8, 8, &lv_font_montserrat_24, ODIN_TEXT);
    text(page, "Power saver", 8, 72, &lv_font_montserrat_16, ODIN_TEXT);
    text(page, "Screen off after 10 min. Touch to wake.", 148, 72, &lv_font_montserrat_14, ODIN_MUTED);
    power_switch = lv_switch_create(page); lv_obj_set_pos(power_switch, 866, 64); lv_obj_set_size(power_switch, 64, 28);
    lv_obj_set_ext_click_area(power_switch, 8);
    lv_obj_set_style_bg_color(power_switch, lv_color_hex(ODIN_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(power_switch, lv_color_hex(ODIN_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(power_switch, lv_color_hex(ODIN_TEXT), LV_PART_KNOB);
    if (odin_power_is_enabled()) lv_obj_add_state(power_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(power_switch, power_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    button(page, LV_SYMBOL_LEFT "  Home", 844, 0, 108, home_cb);
    lv_obj_t *battery_card = card(page, 104, 132);
    text(battery_card, LV_SYMBOL_BATTERY_FULL "  Battery", 24, 14, &lv_font_montserrat_18, ODIN_TEXT);
    battery_value = text(battery_card, "Checking...", 24, 46, &lv_font_montserrat_40, ODIN_ACCENT);
    battery_details = text(battery_card, "Waiting for battery status", 440, 44, &lv_font_montserrat_18, ODIN_TEXT);
    lv_obj_set_style_text_line_space(battery_details, 6, 0);
    battery_note = text(battery_card, "Readings update automatically.", 24, 108, &lv_font_montserrat_14, ODIN_MUTED);
    lv_obj_t *audio_card = card(page, 252, 112);
    text(audio_card, LV_SYMBOL_VOLUME_MAX "  Speaker volume", 24, 14, &lv_font_montserrat_18, ODIN_TEXT);
    unsigned volume = odin_audio_get_volume();
    volume_value = text(audio_card, "", 548, 14, &lv_font_montserrat_18, ODIN_ACCENT);
    if (volume) lv_label_set_text_fmt(volume_value, "%u%%", volume);
    else lv_label_set_text(volume_value, "Muted");
    volume_slider = lv_slider_create(audio_card); lv_obj_set_pos(volume_slider, 32, 70);
    lv_obj_set_size(volume_slider, 564, 10); lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF); lv_obj_set_ext_click_area(volume_slider, 18);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_cb, LV_EVENT_PRESS_LOST, NULL);
    button(audio_card, LV_SYMBOL_WIFI "  Wi-Fi settings", 696, 44, 220, wifi_cb);
    lv_obj_t *model_card = card(page, 380, 156);
    text(model_card, "AI model", 24, 16, &lv_font_montserrat_18, ODIN_TEXT);
    model_refresh = button(model_card, LV_SYMBOL_REFRESH "  Refresh", 792, 10, 128, model_refresh_cb);
    lv_obj_set_height(model_refresh, 40);
    model_selector = lv_dropdown_create(model_card);
    lv_obj_set_pos(model_selector, 24, 58); lv_obj_set_size(model_selector, 896, 46);
    lv_obj_set_style_bg_color(model_selector, lv_color_hex(ODIN_RAISED), 0);
    lv_obj_set_style_text_color(model_selector, lv_color_hex(ODIN_TEXT), 0);
    lv_obj_set_style_border_color(model_selector, lv_color_hex(ODIN_LINE), 0);
    lv_obj_set_style_radius(model_selector, 10, 0);
    lv_dropdown_set_options(model_selector, "Loading models...");
    lv_obj_add_state(model_selector, LV_STATE_DISABLED);
    lv_obj_t *list = lv_dropdown_get_list(model_selector);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(ODIN_RAISED), 0);
        lv_obj_set_style_text_color(list, lv_color_hex(ODIN_TEXT), 0);
        lv_obj_set_style_max_height(list, 240, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(ODIN_ACCENT), LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, lv_color_hex(ODIN_BG), LV_PART_SELECTED | LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(model_selector, model_selected_cb, LV_EVENT_VALUE_CHANGED, NULL);
    model_status = text(model_card, "Loading models...", 24, 120, &lv_font_montserrat_14, ODIN_MUTED);
    lv_obj_set_width(model_status, 896); lv_label_set_long_mode(model_status, LV_LABEL_LONG_DOT);
    update_models();
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(poll, 250, NULL);
    lv_mem_monitor_t memory; lv_mem_monitor(&memory);
    ESP_LOGI("odin_settings", "LVGL heap: %u%% used, %u bytes free, largest block %u",
             memory.used_pct, (unsigned)memory.free_size, (unsigned)memory.free_biggest_size);
    if (xTaskCreate(battery_worker, "odin_battery", 4096, NULL, 2, NULL) != pdPASS)
        lv_label_set_text(battery_details, "Battery monitoring could not start");
}
