#include "odin_wifi_ui.h"
#include "odin_wifi.h"
#include "odin_ui.h"
#include "odin_settings.h"
#include "lvgl.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NETWORKS 16

typedef enum { SCAN, CONNECT, DISCONNECT, FORGET } command_t;
typedef struct {
    command_t command;
    char ssid[33];
    char password[64];
} request_t;
typedef struct {
    bool ready;
    bool scanned;
    uint16_t count;
    wifi_ap_record_t aps[MAX_NETWORKS];
    char message[128];
} result_t;

static QueueHandle_t requests, results;
static lv_obj_t *dialog, *network_list, *ssid_input, *password_input, *keyboard;
static lv_obj_t *message_label, *save_note, *scan_button, *connect_button, *disconnect_button, *forget_button;
static lv_obj_t *home_status;
static lv_obj_t *keyboard_window, *keyboard_title;
static bool busy = true, ready, was_connected;
static result_t latest;

static void worker(void *arg)
{
    result_t *out = calloc(1, sizeof(*out));
    if (!out) { vTaskDelete(NULL); return; }
    esp_err_t err = nvs_flash_init();
    /* Do not erase existing NVS automatically on initialization errors. */
    if (err == ESP_OK) err = odin_wifi_init();
    out->ready = err == ESP_OK;
    if (err == ESP_OK) {
        snprintf(out->message, sizeof(out->message), "Wi-Fi ready. Scan or enter a network name.");
    } else {
        snprintf(out->message, sizeof(out->message), "Wi-Fi initialization failed: %s", esp_err_to_name(err));
    }
    if (err != ESP_OK) {
        xQueueSend(results, out, portMAX_DELAY);
        free(out); vTaskDelete(NULL); return;
    }
    err = odin_wifi_connect_saved();
    if (err == ESP_OK) {
        snprintf(out->message, sizeof(out->message), "Saved network connected.");
    } else {
        out->count = MAX_NETWORKS;
        err = odin_wifi_scan(out->aps, &out->count);
        out->scanned = err == ESP_OK;
        snprintf(out->message, sizeof(out->message), err == ESP_OK
                 ? "Choose a network or enter a hidden SSID."
                 : "Wi-Fi ready. Tap Scan to find nearby networks.");
    }
    xQueueSend(results, out, portMAX_DELAY);
    request_t req;
    while (xQueueReceive(requests, &req, portMAX_DELAY)) {
        memset(out, 0, sizeof(*out));
        out->ready = true;
        if (req.command == SCAN) {
            out->count = MAX_NETWORKS;
            err = odin_wifi_scan(out->aps, &out->count);
            out->scanned = err == ESP_OK;
            if (err == ESP_OK) snprintf(out->message, sizeof(out->message), "%u networks found. Tap one or enter a hidden SSID.", out->count);
            else snprintf(out->message, sizeof(out->message), "Scan failed: %s. Tap Scan to retry.", esp_err_to_name(err));
        } else if (req.command == CONNECT) {
            err = odin_wifi_connect(req.ssid, req.password);
            if (err == ESP_OK) snprintf(out->message, sizeof(out->message), "Connected. Network and password saved automatically.");
            else snprintf(out->message, sizeof(out->message), "Connection failed: %s. Check the password and signal.", esp_err_to_name(err));
        } else if (req.command == FORGET) {
            err = odin_wifi_forget();
            snprintf(out->message, sizeof(out->message), err == ESP_OK
                     ? "Network forgotten. Enter credentials to connect again."
                     : "Could not fully forget/disconnect the network. Please retry.");
        } else {
            err = odin_wifi_disconnect();
            snprintf(out->message, sizeof(out->message), err == ESP_OK ? "Disconnected." : "Disconnect failed.");
        }
        memset(&req, 0, sizeof(req));
        xQueueSend(results, out, portMAX_DELAY);
    }
}

static void update_controls(void)
{
    lv_obj_t *buttons[] = {scan_button, connect_button, disconnect_button, forget_button};
    for (unsigned i = 0; i < 4; i++) {
        if (busy || !ready) lv_obj_add_state(buttons[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(buttons[i], LV_STATE_DISABLED);
    }
}

static void hide_keyboard(void)
{
    lv_keyboard_set_textarea(keyboard, NULL);
    lv_obj_add_flag(keyboard_window, LV_OBJ_FLAG_HIDDEN);
}

static void show_keyboard(lv_obj_t *input)
{
    lv_keyboard_set_textarea(keyboard, input);
    lv_label_set_text(keyboard_title, input == ssid_input
                      ? "Network name - Done to enter password"
                      : "Wi-Fi password - Done to finish");
    lv_obj_clear_flag(keyboard_window, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard_window);
}

static void dismiss_keyboard_cb(lv_event_t *e) { hide_keyboard(); }

static void drag_keyboard_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t delta;
    lv_indev_get_vect(indev, &delta);
    lv_coord_t x = lv_obj_get_x(keyboard_window) + delta.x;
    lv_coord_t y = lv_obj_get_y(keyboard_window) + delta.y;
    lv_coord_t max_x = lv_disp_get_hor_res(NULL) - lv_obj_get_width(keyboard_window);
    lv_coord_t max_y = lv_disp_get_ver_res(NULL) - lv_obj_get_height(keyboard_window);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
    lv_obj_set_pos(keyboard_window, x, y);
}

static void send_request(command_t cmd)
{
    if (busy || !ready) return;
    request_t req = {.command = cmd};
    if (cmd == CONNECT) {
        const char *ssid = lv_textarea_get_text(ssid_input);
        const char *password = lv_textarea_get_text(password_input);
        size_t slen = strlen(ssid), plen = strlen(password);
        if (!slen || slen > 32 || plen > 63 || (plen && plen < 8)) {
            lv_label_set_text(message_label, "Enter a network name (up to 32 bytes) and a password of 8-63 bytes, or leave it empty for an open network.");
            return;
        }
        memcpy(req.ssid, ssid, slen);
        memcpy(req.password, password, plen);
    }
    if (xQueueSend(requests, &req, 0) == pdTRUE) {
        busy = true;
        update_controls();
        lv_label_set_text(message_label, cmd == SCAN ? "Scanning 2.4 GHz networks..." : cmd == CONNECT ? "Connecting..." : cmd == FORGET ? "Forgetting network..." : "Disconnecting...");
        if (cmd == CONNECT || cmd == FORGET) {
            lv_textarea_set_text(password_input, "");
            if (cmd == FORGET) lv_textarea_set_text(ssid_input, "");
            hide_keyboard();
        }
    }
    memset(&req, 0, sizeof(req));
}
static void scan_cb(lv_event_t *e) { send_request(SCAN); }
static void connect_cb(lv_event_t *e) { send_request(CONNECT); }
static void disconnect_cb(lv_event_t *e) { send_request(DISCONNECT); }
static void forget_cb(lv_event_t *e) { send_request(FORGET); }
static void close_cb(lv_event_t *e)
{
    lv_textarea_set_text(password_input, "");
    hide_keyboard();
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);
    odin_settings_set_home(true);
}
void odin_wifi_ui_open(void) { odin_settings_set_home(false); lv_obj_clear_flag(dialog, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(dialog); }
static void focus_cb(lv_event_t *e)
{
    show_keyboard(lv_event_get_target(e));
}
static void keyboard_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        if (lv_keyboard_get_textarea(keyboard) == ssid_input) show_keyboard(password_input);
        else hide_keyboard();
    } else if (lv_event_get_code(e) == LV_EVENT_CANCEL) hide_keyboard();
}
static void select_cb(lv_event_t *e)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (index >= latest.count) return;
    char ssid[33] = {0};
    memcpy(ssid, latest.aps[index].ssid, 32);
    lv_textarea_set_text(ssid_input, ssid);
    lv_textarea_set_text(password_input, "");
    show_keyboard(password_input);
    lv_label_set_text(message_label, latest.aps[index].authmode == WIFI_AUTH_OPEN ? "Open network. Leave password empty and tap Connect." : "Enter the network password, then tap Connect.");
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, 48);
    odin_ui_button_style(b, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(b); lv_label_set_text(label, text); lv_obj_center(label);
    return b;
}
static void poll(lv_timer_t *timer)
{
    static result_t result;
    while (xQueueReceive(results, &result, 0)) {
        ready = result.ready;
        busy = false;
        lv_label_set_text(message_label, result.message);
        if (result.scanned) {
            latest = result;
            lv_obj_clean(network_list);
            for (unsigned i = 0; i < latest.count; i++) {
                char name[33] = {0}, label[80];
                memcpy(name, latest.aps[i].ssid, 32);
                snprintf(label, sizeof(label), "%s  %d dBm%s", name[0] ? name : "(hidden)", latest.aps[i].rssi,
                         latest.aps[i].authmode == WIFI_AUTH_OPEN ? "  Open" : "");
                lv_obj_t *b = lv_list_add_btn(network_list, LV_SYMBOL_WIFI, label);
                odin_ui_button_style(b, 0);
                lv_obj_set_style_radius(b, 8, 0); lv_obj_set_style_border_width(b, 0, 0);
                lv_obj_set_style_pad_top(b, 16, 0); lv_obj_set_style_pad_bottom(b, 16, 0);
                lv_obj_add_event_cb(b, select_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            }
        }
        update_controls();
    }
    bool connected = odin_wifi_is_connected();
    if (connected) {
        esp_netif_ip_info_t ip;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            char address[48]; snprintf(address, sizeof(address), LV_SYMBOL_WIFI "  " IPSTR, IP2STR(&ip.ip));
            if (strcmp(lv_label_get_text(home_status), address)) lv_label_set_text(home_status, address);
        }
    } else if (!busy || was_connected) {
        lv_label_set_text(home_status, ready ? "Wi-Fi: disconnected" : "Wi-Fi: unavailable");
    }
    was_connected = connected;
}

void odin_wifi_ui_create(void)
{
    requests = xQueueCreate(1, sizeof(request_t));
    results = xQueueCreate(3, sizeof(result_t));
    if (!requests || !results) return;
    lv_obj_t *scr = lv_scr_act();
    home_status = lv_label_create(scr); lv_label_set_text(home_status, "Wi-Fi: starting...");
    lv_obj_set_pos(home_status, 650, 554); lv_obj_set_width(home_status, 326);
    lv_obj_set_style_text_font(home_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(home_status, lv_color_hex(ODIN_MUTED), 0);
    lv_obj_set_style_text_align(home_status, LV_TEXT_ALIGN_RIGHT, 0);
    dialog = lv_obj_create(scr); lv_obj_set_size(dialog, 1024, 600); lv_obj_center(dialog);
    odin_ui_panel_style(dialog);
    lv_obj_t *title = lv_label_create(dialog); lv_label_set_text(title, "A place to connect."); lv_obj_set_pos(title, 8, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    button(dialog, LV_SYMBOL_LEFT "  Home", 844, 0, 108, close_cb);
    message_label = lv_label_create(dialog); lv_obj_set_pos(message_label, 8, 68); lv_obj_set_width(message_label, 940);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP); lv_label_set_text(message_label, "Starting Wi-Fi...");
    lv_obj_set_style_text_color(message_label, lv_color_hex(ODIN_MUTED), 0);
    network_list = lv_list_create(dialog); lv_obj_set_pos(network_list, 8, 112); lv_obj_set_size(network_list, 350, 408);
    lv_obj_set_style_bg_color(network_list, lv_color_hex(ODIN_SURFACE), 0);
    lv_obj_set_style_border_width(network_list, 0, 0); lv_obj_set_style_radius(network_list, 14, 0);
    ssid_input = lv_textarea_create(dialog); lv_obj_set_pos(ssid_input, 382, 112); lv_obj_set_size(ssid_input, 570, 58);
    odin_ui_input_style(ssid_input);
    lv_textarea_set_one_line(ssid_input, true); lv_textarea_set_max_length(ssid_input, 32); lv_textarea_set_placeholder_text(ssid_input, "Network name (SSID)");
    lv_obj_add_event_cb(ssid_input, focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ssid_input, focus_cb, LV_EVENT_CLICKED, NULL);
    password_input = lv_textarea_create(dialog); lv_obj_set_pos(password_input, 382, 188); lv_obj_set_size(password_input, 570, 58);
    odin_ui_input_style(password_input);
    lv_textarea_set_one_line(password_input, true); lv_textarea_set_max_length(password_input, 63); lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_placeholder_text(password_input, "Password (empty for open networks)");
    lv_obj_add_event_cb(password_input, focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(password_input, focus_cb, LV_EVENT_CLICKED, NULL);
    save_note = lv_label_create(dialog); lv_obj_set_pos(save_note, 382, 266);
    lv_label_set_text(save_note, "Your network is remembered after connecting.");
    lv_obj_set_style_text_color(save_note, lv_color_hex(ODIN_MUTED), 0);
    lv_obj_set_style_text_font(save_note, &lv_font_montserrat_14, 0);
    scan_button = button(dialog, "Scan", 382, 308, 100, scan_cb);
    connect_button = button(dialog, "Connect", 494, 308, 136, connect_cb);
    disconnect_button = button(dialog, "Disconnect", 642, 308, 144, disconnect_cb);
    forget_button = button(dialog, "Forget", 798, 308, 154, forget_cb);
    odin_ui_button_style(connect_button, 1); odin_ui_button_style(forget_button, 2);
    /* Top-layer window floats above the Wi-Fi dialog without moving its fields. */
    keyboard_window = lv_obj_create(lv_layer_top());
    lv_obj_set_size(keyboard_window, 960, 258);
    lv_obj_set_pos(keyboard_window, 32, 328);
    lv_obj_set_style_pad_all(keyboard_window, 8, 0);
    lv_obj_set_style_radius(keyboard_window, 14, 0);
    lv_obj_set_style_bg_color(keyboard_window, lv_color_hex(ODIN_SURFACE), 0);
    lv_obj_set_style_border_color(keyboard_window, lv_color_hex(ODIN_LINE), 0);
    lv_obj_set_style_border_width(keyboard_window, 1, 0);
    lv_obj_set_style_shadow_width(keyboard_window, 24, 0);
    lv_obj_clear_flag(keyboard_window, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *handle = lv_obj_create(keyboard_window);
    lv_obj_set_pos(handle, 0, 0); lv_obj_set_size(handle, 828, 42);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(handle, drag_keyboard_cb, LV_EVENT_PRESSING, NULL);
    keyboard_title = lv_label_create(handle);
    lv_label_set_text(keyboard_title, "Wi-Fi keyboard");
    lv_obj_set_style_text_color(keyboard_title, lv_color_hex(ODIN_TEXT), 0);
    lv_obj_center(keyboard_title);
    button(keyboard_window, "Hide", 838, 0, 100, dismiss_keyboard_cb);
    keyboard = lv_keyboard_create(keyboard_window);
    lv_obj_set_pos(keyboard, 0, 50); lv_obj_set_size(keyboard, 940, 188);
    odin_ui_keyboard_style(keyboard);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
    hide_keyboard();
    lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);
    update_controls();
    lv_timer_create(poll, 250, NULL);
    if (xTaskCreate(worker, "odin_wifi", 8192, NULL, 5, NULL) != pdPASS) {
        busy = false;
        lv_label_set_text(message_label, "Unable to start Wi-Fi worker.");
    }
}
