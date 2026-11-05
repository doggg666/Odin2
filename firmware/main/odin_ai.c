#include "odin_ai.h"
#include "odin_ui.h"
#include "odin_wifi.h"
#include "odin_audio.h"
#include "odin_voice.h"
#include <stdatomic.h>
#include "lvgl.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define API_URL "http://192.168.1.93:1234/v1"
#define BODY_LIMIT 32768
#define PROMPT_LIMIT 768
#define REPLY_LIMIT 6144
static const char *TAG = "odin_ai";
static QueueHandle_t requests, results;
static lv_obj_t *dialog, *prompt, *reply, *status, *send_button, *keyboard;
static bool busy;
static lv_obj_t *volume_slider, *volume_label;
static atomic_bool recording;
static lv_obj_t *talk_button, *audio_test_button, *speak_checkbox;
typedef struct { char text[PROMPT_LIMIT]; bool voice, test, speak; } request_t;
typedef struct { bool online, progress, background; char note[160]; char text[REPLY_LIMIT]; } result_t;
typedef struct { char *data; size_t used; bool overflow; } body_t;

static esp_err_t http_event(esp_http_client_event_t *e)
{
    body_t *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (e->data_len < 0 || b->used + (size_t)e->data_len >= BODY_LIMIT) {
            b->overflow = true;
            return ESP_FAIL;
        }
        memcpy(b->data + b->used, e->data, e->data_len);
        b->used += e->data_len;
        b->data[b->used] = 0;
    }
    return ESP_OK;
}

static cJSON *http_json(const char *suffix, const char *payload, char *error, size_t size)
{
    char url[128]; snprintf(url, sizeof(url), API_URL "%s", suffix);
    body_t body = {.data = calloc(1, BODY_LIMIT)};
    if (!body.data) { snprintf(error, size, "Not enough memory for response."); return NULL; }
    esp_http_client_config_t cfg = {
        .url = url, .timeout_ms = payload ? 90000 : 5000,
        .event_handler = http_event, .user_data = &body,
        .buffer_size = 2048, .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { free(body.data); snprintf(error, size, "Unable to create HTTP client."); return NULL; }
    if (payload) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, payload, strlen(payload));
    }
    esp_err_t err = esp_http_client_perform(client);
    int code = esp_http_client_get_status_code(client);
    cJSON *json = NULL;
    if (body.overflow) snprintf(error, size, "Server response is too large. Try a shorter question.");
    else if (err != ESP_OK) snprintf(error, size, "LM Studio unreachable: %s. Check Wi-Fi and server LAN access.", esp_err_to_name(err));
    else if (code < 200 || code >= 300) snprintf(error, size, "LM Studio returned HTTP %d. Check the loaded model and server settings.", code);
    else {
        json = cJSON_Parse(body.data);
        if (!json) snprintf(error, size, "LM Studio returned invalid JSON.");
    }
    esp_http_client_cleanup(client); free(body.data);
    return json;
}

static bool discover_model(char *model, size_t size, char *error, size_t error_size)
{
    cJSON *json = http_json("/models", NULL, error, error_size);
    if (!json) return false;
    cJSON *item;
    bool found = false;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(json, "data")) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (cJSON_IsString(id) && id->valuestring[0] && !strstr(id->valuestring, "embedding")) {
            if (strlen(id->valuestring) >= size) continue;
            snprintf(model, size, "%s", id->valuestring); found = true; break;
        }
    }
    cJSON_Delete(json);
    if (!found) snprintf(error, error_size, "No chat model found. Load a chat model in LM Studio.");
    return found;
}

static void worker(void *arg)
{
    result_t *out = calloc(1, sizeof(*out));
    request_t *req = calloc(1, sizeof(*req));
    if (!out || !req) { free(out); free(req); vTaskDelete(NULL); return; }
    char model[192] = {0};
    esp_err_t audio_err = odin_audio_init();
    if (audio_err != ESP_OK) ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(audio_err));
    bool probed = false;
    for (;;) {
        if (!xQueueReceive(requests, req, pdMS_TO_TICKS(500))) {
            if (!probed && odin_wifi_is_connected()) {
                memset(out, 0, sizeof(*out));
                out->background = true;
                out->online = discover_model(model, sizeof(model), out->text, sizeof(out->text));
                if (out->online) {
                    snprintf(out->text, sizeof(out->text), "LM Studio is ready. Type a message or hold to talk.");
                    ESP_LOGI(TAG, "LM Studio model discovery succeeded");
                }
                xQueueSend(results, out, portMAX_DELAY);
                probed = true;
            }
            continue;
        }
        memset(out, 0, sizeof(*out));
        bool process = true;
        if (req->voice || req->test) {
            int16_t *pcm = NULL; size_t bytes = 0;
            esp_err_t err = odin_audio_record(&recording, &pcm, &bytes);
            if (err != ESP_OK) {
                snprintf(out->text, sizeof(out->text), "Recording failed (%s). Hold the button for at least a quarter second.", esp_err_to_name(err));
                process = false;
            } else if (req->test) {
                err = odin_audio_play(pcm, bytes);
                snprintf(out->text, sizeof(out->text), err == ESP_OK ? "Recording played back. If you heard your voice, the mic and speaker path work." : "Audio playback failed: %s", esp_err_to_name(err));
                process = false;
            } else {
                out->progress = true;
                snprintf(out->note, sizeof(out->note), "Transcribing...");
                xQueueSend(results, out, portMAX_DELAY);
                out->progress = false;
                err = odin_voice_transcribe(pcm, bytes, req->text, sizeof(req->text));
                if (err != ESP_OK) { snprintf(out->text, sizeof(out->text), "%s", req->text); process = false; }
            }
            free(pcm);
        }
        if (!process) {
            out->note[0] = 0;
            xQueueSend(results, out, portMAX_DELAY);
            memset(req, 0, sizeof(*req));
            continue;
        }
        if (req->voice) {
            out->progress = true;
            snprintf(out->text, sizeof(out->text), "You: %s", req->text);
            snprintf(out->note, sizeof(out->note), "Odin is thinking...");
            xQueueSend(results, out, portMAX_DELAY);
            out->progress = false; out->note[0] = 0; out->text[0] = 0;
        }
        if (!odin_wifi_is_connected()) snprintf(out->text, sizeof(out->text), "Connect to Wi-Fi first.");
        else if (discover_model(model, sizeof(model), out->text, sizeof(out->text))) {
            cJSON *root = cJSON_CreateObject();
            if (root) {
                cJSON_AddStringToObject(root, "model", model);
                cJSON_AddBoolToObject(root, "stream", false);
                cJSON_AddNumberToObject(root, "max_tokens", 512);
                cJSON *messages = cJSON_AddArrayToObject(root, "messages");
                cJSON *system = cJSON_CreateObject();
                cJSON_AddStringToObject(system, "role", "system");
                cJSON_AddStringToObject(system, "content", "You are Odin, a helpful assistant on a small touch display. Answer concisely in plain text.");
                cJSON_AddItemToArray(messages, system);
                cJSON *user = cJSON_CreateObject();
                cJSON_AddStringToObject(user, "role", "user");
                cJSON_AddStringToObject(user, "content", req->text);
                cJSON_AddItemToArray(messages, user);
                char *payload = cJSON_PrintUnformatted(root);
                cJSON_Delete(root);
                cJSON *json = payload ? http_json("/chat/completions", payload, out->text, sizeof(out->text)) : NULL;
                free(payload);
                if (json) {
                    cJSON *choice = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(json, "choices"), 0);
                    cJSON *message = cJSON_GetObjectItemCaseSensitive(choice, "message");
                    cJSON *content = cJSON_GetObjectItemCaseSensitive(message, "content");
                    if (cJSON_IsString(content) && content->valuestring[0]) {
                        snprintf(out->text, sizeof(out->text), "%s", content->valuestring);
                        out->online = true;
                        ESP_LOGI(TAG, "LM Studio reply received");
                    } else snprintf(out->text, sizeof(out->text), "Model returned no answer. Try again or select a different model in LM Studio.");
                    cJSON_Delete(json);
                }
            }
            if (!out->text[0]) snprintf(out->text, sizeof(out->text), "Not enough memory to send the request.");
        }
        if (out->online && req->speak) {
            out->progress = true;
            snprintf(out->note, sizeof(out->note), "Speaking...");
            xQueueSend(results, out, portMAX_DELAY);
            out->progress = false; out->note[0] = 0;
            esp_err_t err = odin_voice_speak(out->text, out->note, sizeof(out->note));
            if (err != ESP_OK && !out->note[0]) snprintf(out->note, sizeof(out->note), "Text reply ready; audio failed: %s", esp_err_to_name(err));
        }
        memset(req, 0, sizeof(*req));
        xQueueSend(results, out, portMAX_DELAY);
    }
}
static void hide_keyboard(void) { lv_keyboard_set_textarea(keyboard, NULL); lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN); }
static void focus_cb(lv_event_t *e) { lv_keyboard_set_textarea(keyboard, prompt); lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN); }
static void keyboard_cb(lv_event_t *e) { hide_keyboard(); }
static void open_cb(lv_event_t *e) { lv_obj_clear_flag(dialog, LV_OBJ_FLAG_HIDDEN); }
static void close_cb(lv_event_t *e) { atomic_store(&recording, false); hide_keyboard(); lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN); }
static void send_cb(lv_event_t *e)
{
    if (busy) return;
    request_t req = {0};
    const char *text = lv_textarea_get_text(prompt);
    if (!text[0]) { lv_label_set_text(status, "Type a message first."); return; }
    if (strlen(text) >= sizeof(req.text)) { lv_label_set_text(status, "Message is too long."); return; }
    if (!odin_wifi_is_connected()) { lv_label_set_text(status, "Connect to Wi-Fi first."); return; }
    req.speak = lv_obj_has_state(speak_checkbox, LV_STATE_CHECKED);
    snprintf(req.text, sizeof(req.text), "%s", text);
    if (xQueueSend(requests, &req, 0)) {
        busy = true; hide_keyboard(); lv_obj_add_state(send_button, LV_STATE_DISABLED);
        lv_label_set_text(status, "Odin is thinking...");
        odin_ui_set_status("Thinking...");
    }
}
static void recording_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        atomic_store(&recording, false);
        return;
    }
    if (code != LV_EVENT_PRESSED || busy) return;
    bool test = lv_event_get_target(e) == audio_test_button;
    if (!test && !odin_wifi_is_connected()) {
        lv_label_set_text(status, "Connect to Wi-Fi first."); return;
    }
    request_t req = {.voice = !test, .test = test,
                     .speak = lv_obj_has_state(speak_checkbox, LV_STATE_CHECKED)};
    atomic_store(&recording, true);
    if (xQueueSend(requests, &req, 0)) {
        busy = true; hide_keyboard(); lv_obj_add_state(send_button, LV_STATE_DISABLED);
        lv_label_set_text(status, "Listening... release when you are done (up to 12 seconds)");
        odin_ui_set_status("Listening...");
        lv_obj_set_style_bg_color(lv_event_get_target(e), lv_color_hex(0xECD2A1), 0);
    } else atomic_store(&recording, false);
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_btn_create(parent); lv_obj_set_pos(b, x, y); lv_obj_set_size(b, width, 52);
    odin_ui_button_style(b, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(b); lv_label_set_text(label, text); lv_obj_center(label); return b;
}
static void volume_changed(lv_event_t *e)
{
    unsigned volume = lv_slider_get_value(volume_slider);
    odin_audio_set_volume(volume);
    if (volume) lv_label_set_text_fmt(volume_label, "Volume  %u%%", volume);
    else lv_label_set_text(volume_label, "Volume  Muted");
    if (lv_event_get_code(e) == LV_EVENT_RELEASED ||
        lv_event_get_code(e) == LV_EVENT_PRESS_LOST) odin_audio_save_volume();
}
static void ai_poll(lv_timer_t *t)
{
    unsigned volume = odin_audio_get_volume();
    if (lv_slider_get_value(volume_slider) != volume) {
        lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
        if (volume) lv_label_set_text_fmt(volume_label, "Volume  %u%%", volume);
        else lv_label_set_text(volume_label, "Volume  Muted");
    }
    static result_t result;
    while (xQueueReceive(results, &result, 0)) {
        if (result.background && busy) continue;
        if (!result.progress) {
            busy = false; lv_obj_clear_state(send_button, LV_STATE_DISABLED);
            odin_ui_button_style(talk_button, 1); odin_ui_button_style(audio_test_button, 0);
            odin_ui_set_status("Ready when you are");
            lv_label_set_text(status, result.note[0] ? result.note : result.online ? "LM Studio connected" : "Ready - check the message below");
            odin_ui_set_mode(result.online ? "LM STUDIO" : "OFFLINE");
        } else { lv_label_set_text(status, result.note); odin_ui_set_status(result.note); }
        if (result.text[0]) lv_textarea_set_text(reply, result.text);
    }
}
void odin_ai_create(void)
{
    requests = xQueueCreate(1, sizeof(request_t)); results = xQueueCreate(2, sizeof(result_t));
    if (!requests || !results) return;
    lv_obj_t *home_chat = button(lv_scr_act(), LV_SYMBOL_AUDIO "  Talk to Odin", 56, 420, 250, open_cb);
    lv_obj_set_height(home_chat, 60); odin_ui_button_style(home_chat, 1);
    dialog = lv_obj_create(lv_scr_act()); lv_obj_set_size(dialog, 1024, 600); lv_obj_center(dialog);
    odin_ui_panel_style(dialog);
    lv_obj_t *title = lv_label_create(dialog); lv_label_set_text(title, "Let's talk."); lv_obj_set_pos(title, 8, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    button(dialog, LV_SYMBOL_LEFT "  Home", 844, 0, 108, close_cb);
    status = lv_label_create(dialog); lv_label_set_text(status, "Getting things ready..."); lv_obj_set_pos(status, 8, 68);
    lv_obj_set_width(status, 940);
    lv_obj_set_style_text_color(status, lv_color_hex(ODIN_MUTED), 0);
    reply = lv_textarea_create(dialog); lv_obj_set_pos(reply, 8, 112); lv_obj_set_size(reply, 944, 150);
    odin_ui_input_style(reply);
    lv_obj_clear_flag(reply, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_textarea_set_text(reply, "What's on your mind?\nType a message, or hold the microphone button to speak.");
    prompt = lv_textarea_create(dialog); lv_obj_set_pos(prompt, 8, 278); lv_obj_set_size(prompt, 754, 58);
    odin_ui_input_style(prompt);
    lv_textarea_set_one_line(prompt, true); lv_textarea_set_max_length(prompt, PROMPT_LIMIT - 1);
    lv_textarea_set_placeholder_text(prompt, "Ask anything...");
    lv_obj_add_event_cb(prompt, focus_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(prompt, focus_cb, LV_EVENT_FOCUSED, NULL);
    send_button = button(dialog, "Send " LV_SYMBOL_RIGHT, 776, 278, 176, send_cb);
    lv_obj_set_height(send_button, 58); odin_ui_button_style(send_button, 1);
    talk_button = button(dialog, LV_SYMBOL_AUDIO "  Hold to talk", 8, 360, 240, NULL);
    lv_obj_set_height(talk_button, 60); odin_ui_button_style(talk_button, 1);
    audio_test_button = button(dialog, "Hold to test mic", 264, 360, 212, NULL);
    lv_obj_set_height(audio_test_button, 60);
    lv_obj_add_event_cb(talk_button, recording_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(audio_test_button, recording_cb, LV_EVENT_ALL, NULL);
    speak_checkbox = lv_checkbox_create(dialog); lv_obj_set_pos(speak_checkbox, 520, 380);
    lv_checkbox_set_text(speak_checkbox, "Speak replies"); lv_obj_add_state(speak_checkbox, LV_STATE_CHECKED);
    lv_obj_t *voice_hint = lv_label_create(dialog); lv_obj_set_pos(voice_hint, 8, 492);
    lv_obj_set_style_text_color(voice_hint, lv_color_hex(ODIN_MUTED), 0);
    lv_obj_set_style_text_line_space(voice_hint, 8, 0);
    lv_label_set_text(voice_hint, "Hold to record. Release to send.");
    volume_label = lv_label_create(dialog); lv_obj_set_pos(volume_label, 8, 452);
    lv_obj_set_style_text_color(volume_label, lv_color_hex(ODIN_MUTED), 0);
    unsigned volume = odin_audio_get_volume();
    if (volume) lv_label_set_text_fmt(volume_label, "Volume  %u%%", volume);
    else lv_label_set_text(volume_label, "Volume  Muted");
    volume_slider = lv_slider_create(dialog); lv_obj_set_pos(volume_slider, 208, 458);
    lv_obj_set_size(volume_slider, 716, 10); lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(volume_slider, 18);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(ODIN_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_PRESS_LOST, NULL);
    keyboard = lv_keyboard_create(dialog); lv_obj_set_pos(keyboard, 8, 348); lv_obj_set_size(keyboard, 944, 198);
    odin_ui_keyboard_style(keyboard);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
    hide_keyboard(); lv_obj_add_flag(dialog, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(ai_poll, 250, NULL);
    if (xTaskCreate(worker, "odin_ai", 8192, NULL, 4, NULL) != pdPASS)
        lv_label_set_text(status, "Unable to start AI worker.");
}
