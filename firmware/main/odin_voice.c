#include "odin_voice.h"
#include "odin_audio.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *data; size_t used, capacity; bool overflow; } response_t;
static esp_err_t receive(esp_http_client_event_t *e)
{
    response_t *r = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA) {
        if (e->data_len < 0 || (size_t)e->data_len >= r->capacity - r->used) {
            r->overflow = true; return ESP_FAIL;
        }
        memcpy(r->data + r->used, e->data, e->data_len); r->used += e->data_len;
        r->data[r->used] = 0;
    }
    return ESP_OK;
}
static esp_err_t post(const char *path, const char *type, const void *data, size_t length,
                      response_t *response, char *error, size_t size)
{
    char url[128]; snprintf(url, sizeof(url), ODIN_VOICE_URL "%s", path);
    esp_http_client_config_t cfg = {.url = url, .timeout_ms = 90000, .event_handler = receive,
                                   .user_data = response, .buffer_size = 2048};
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", type);
    esp_http_client_set_post_field(client, data, length);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (response->overflow) { snprintf(error, size, "Voice response exceeded the size limit."); err = ESP_ERR_INVALID_SIZE; }
    else if (err != ESP_OK) snprintf(error, size, "Voice server unavailable at " ODIN_VOICE_URL ".");
    else if (status != 200) { snprintf(error, size, "Voice server returned HTTP %d. Check the server log.", status); err = ESP_FAIL; }
    esp_http_client_cleanup(client);
    return err;
}
esp_err_t odin_voice_transcribe(const int16_t *pcm, size_t bytes, char *text, size_t size)
{
    response_t r = {.capacity = 8192, .data = calloc(1, 8192)};
    if (!r.data) return ESP_ERR_NO_MEM;
    char error[160] = {0};
    esp_err_t err = post("/stt", "application/octet-stream", pcm, bytes, &r, error, sizeof(error));
    if (err == ESP_OK) {
        cJSON *json = cJSON_Parse(r.data);
        cJSON *value = cJSON_GetObjectItemCaseSensitive(json, "text");
        if (!cJSON_IsString(value) || !value->valuestring[0]) { snprintf(error, sizeof(error), "No speech detected. Hold the button and speak closer to the mic."); err = ESP_FAIL; }
        else if (strlen(value->valuestring) >= size) { snprintf(error, sizeof(error), "Transcription too long. Try a shorter recording."); err = ESP_ERR_INVALID_SIZE; }
        else snprintf(text, size, "%s", value->valuestring);
        cJSON_Delete(json);
    }
    if (err != ESP_OK) snprintf(text, size, "%s", error[0] ? error : "Unable to transcribe recording.");
    free(r.data); return err;
}
esp_err_t odin_voice_speak(const char *text, char *error, size_t size)
{
    const size_t capacity = 3 * 1024 * 1024;
    response_t r = {.capacity = capacity, .data = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)};
    if (!r.data) return ESP_ERR_NO_MEM;
    cJSON *json = cJSON_CreateObject(); cJSON_AddStringToObject(json, "text", text);
    char *payload = cJSON_PrintUnformatted(json); cJSON_Delete(json);
    esp_err_t err = payload ? post("/tts", "application/json", payload, strlen(payload), &r, error, size) : ESP_ERR_NO_MEM;
    free(payload);
    if (err == ESP_OK) {
        if (!r.used || r.used % 2) { snprintf(error, size, "Voice server returned invalid PCM audio."); err = ESP_ERR_INVALID_SIZE; }
        else {
            err = odin_audio_play((int16_t *)r.data, r.used);
            if (err != ESP_OK) snprintf(error, size, "Speaker playback failed: %s", esp_err_to_name(err));
        }
    }
    free(r.data); return err;
}
